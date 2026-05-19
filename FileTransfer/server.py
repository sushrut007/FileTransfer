"""
Signalling Server  –  local development edition
Socket.IO v4 / Engine.IO v4  (python-socketio + aiohttp)

Install:
    pip install "python-socketio[asyncio_client]" aiohttp

Run:
    python server.py
"""

import asyncio
import logging
import random
import string
import json
from typing import Any

import aiohttp.web
import socketio

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s  %(levelname)-8s  %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("signaling")

# ---------------------------------------------------------------------------
# Socket.IO server  (EIO=4, async mode)
# ---------------------------------------------------------------------------
sio = socketio.AsyncServer(
    async_mode="aiohttp",
    cors_allowed_origins="*",
    logger=False,
    engineio_logger=False,
)

app = aiohttp.web.Application()
sio.attach(app)

# ---------------------------------------------------------------------------
# In-memory registries
#
#   rooms[roomId]    = { socketId: { "userId": sid, "appType": str } }
#   room_passwords[roomId] = password          (plain-text – dev only)
#   transfers[transferId]  = { "senderId": sid, "receiverId": sid }
# ---------------------------------------------------------------------------
rooms:           dict[str, dict[str, dict[str, Any]]] = {}
room_passwords:  dict[str, str]                       = {}
transfers:       dict[str, dict[str, str]]            = {}


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _gen_room_id(length: int = 6) -> str:
    """Generate a random alphanumeric room ID that is not already in use."""
    chars = string.ascii_letters + string.digits
    while True:
        rid = "".join(random.choices(chars, k=length))
        if rid not in rooms:
            return rid


def _peer_list(room_id: str, exclude_sid: str | None = None) -> list[dict]:
    """All peer-info dicts in *room_id*, optionally excluding *exclude_sid*."""
    return [
        info
        for sid, info in rooms.get(room_id, {}).items()
        if sid != exclude_sid
    ]


def _ok(**kwargs) -> dict:
    return {"success": True, **kwargs}


def _err(message: str) -> dict:
    return {"success": False, "message": message}


# ---------------------------------------------------------------------------
# HTTP  –  REST endpoints
#
#   POST /api/v1/create-room   { password }
#       → { success, roomId }
#
#   POST /api/v1/login         { roomId, password }
#       → 200 + Set-Cookie: session=<roomId>
# ---------------------------------------------------------------------------

async def handle_create_room(request: aiohttp.web.Request) -> aiohttp.web.Response:
    """
    Create a new room with a server-generated ID.
    The C++ client (Mode::CreateRoom) calls this before login/join.
    """
    try:
        body = await request.json()
    except Exception:
        return aiohttp.web.Response(
            status=400,
            content_type="application/json",
            text=json.dumps(_err("Invalid JSON body")),
        )

    password: str = str(body.get("password", "")).strip()
    if not password:
        return aiohttp.web.Response(
            status=400,
            content_type="application/json",
            text=json.dumps(_err("password is required")),
        )

    room_id = _gen_room_id()
    # Pre-register the room so the ID is reserved before login
    rooms[room_id]          = {}
    room_passwords[room_id] = password

    log.info("[HTTP] create-room → roomId=%s", room_id)

    return aiohttp.web.Response(
        status=200,
        content_type="application/json",
        text=json.dumps({"success": True, "roomId": room_id}),
    )


async def handle_login(request: aiohttp.web.Request) -> aiohttp.web.Response:
    """
    Validate roomId + password and issue a session cookie.
    For the local dev server we accept any non-empty credentials;
    the cookie value is simply the roomId so the socket handler can
    read it if needed (currently unused by the C++ client).
    """
    try:
        body = await request.json()
    except Exception:
        return aiohttp.web.Response(
            status=400,
            content_type="application/json",
            text=json.dumps(_err("Invalid JSON body")),
        )

    room_id:  str = str(body.get("roomId",  "")).strip()
    password: str = str(body.get("password", "")).strip()

    if not room_id or not password:
        return aiohttp.web.Response(
            status=400,
            content_type="application/json",
            text=json.dumps(_err("roomId and password are required")),
        )

    # Password check (only if the room was pre-created via /create-room)
    stored_pw = room_passwords.get(room_id)
    if stored_pw is not None and stored_pw != password:
        log.warning("[HTTP] login – wrong password for room %s", room_id)
        return aiohttp.web.Response(
            status=401,
            content_type="application/json",
            text=json.dumps(_err("Incorrect password")),
        )

    # If the room was never explicitly created (join-only flow) accept it and
    # register the password so future joins can validate.
    if room_id not in room_passwords:
        room_passwords[room_id] = password

    log.info("[HTTP] login OK  room=%s", room_id)

    response = aiohttp.web.Response(
        status=200,
        content_type="application/json",
        text=json.dumps({"success": True, "roomId": room_id}),
    )
    # The C++ client parses Set-Cookie headers; any name=value pair is fine.
    response.set_cookie("session", room_id, httponly=True, path="/")
    return response


# Register HTTP routes
app.router.add_post("/api/v1/create-room", handle_create_room)
app.router.add_post("/api/v1/login",       handle_login)


# ---------------------------------------------------------------------------
# Socket.IO – lifecycle
# ---------------------------------------------------------------------------

@sio.event
async def connect(sid: str, environ: dict, auth: Any = None) -> None:
    log.info("[CONNECT]   sid=%s", sid)


@sio.event
async def disconnect(sid: str) -> None:
    log.info("[DISCONNECT] sid=%s", sid)

    for room_id, peers in list(rooms.items()):
        if sid in peers:
            peer_info = peers.pop(sid)
            await sio.leave_room(sid, room_id)
            log.info("[ROOM] %s left room %s", sid, room_id)

            await sio.emit(
                "peer-left",
                {"userId": peer_info["userId"], "roomId": room_id},
                room=room_id,
            )

            if not peers:
                del rooms[room_id]
                # Keep room_passwords so a reconnect still works;
                # clear it explicitly only if you want rooms to expire.
                log.debug("[ROOM] %s is now empty", room_id)


# ---------------------------------------------------------------------------
# Socket.IO – room join
#
# The C++ client sends the event name "join-room" (hyphenated).
# python-socketio maps hyphenated names via sio.on().
# ---------------------------------------------------------------------------

@sio.on("join-room")
async def on_join_room(sid: str, data: dict) -> dict:
    """
    Payload : { roomId, password, appType? }
    Ack     : { success, data: { roomId, peers } }
              (matches the C++ handleJoinSuccess parser)
    """
    room_id:  str = str(data.get("roomId",  "")).strip()
    password: str = str(data.get("password", "")).strip()
    app_type: str = str(data.get("appType", "unknown"))

    if not room_id:
        log.warning("[JOIN] sid=%s – empty roomId", sid)
        return _err("roomId is required")

    # Validate password if the room has one recorded
    stored_pw = room_passwords.get(room_id)
    if stored_pw is not None and stored_pw != password:
        log.warning("[JOIN] sid=%s – wrong password for room %s", sid, room_id)
        return _err("Incorrect password")

    # Create room on first join (join-only flow)
    if room_id not in rooms:
        rooms[room_id]          = {}
        room_passwords[room_id] = password

    peer_info = {"userId": sid, "appType": app_type}
    rooms[room_id][sid] = peer_info

    await sio.enter_room(sid, room_id)
    log.info("[JOIN]  sid=%s  room=%s  appType=%s", sid, room_id, app_type)

    # Existing peers for the ack (before this peer is excluded)
    existing_peers = _peer_list(room_id, exclude_sid=sid)

    # Push "joined" only to the caller so it can capture its own userId/peerId
    await sio.emit("joined", {"userId": sid, "roomId": room_id}, to=sid)

    # Notify every other peer
    await sio.emit(
        "new-peer",
        {"userId": sid, "roomId": room_id, "appType": app_type},
        room=room_id,
        skip_sid=sid,
    )

    return _ok(data={"roomId": room_id, "peers": existing_peers})


# ---------------------------------------------------------------------------
# Socket.IO – file-transfer events
#
# All event names are hyphenated to match the C++ SocketIOClient exactly.
# Events that the C++ caller expects an ack for return { success: True }.
# ---------------------------------------------------------------------------

async def _relay(event: str, sid: str, data: dict, tag: str) -> None:
    """Forward *data* (with fromPeerId stamped) to data['targetPeerId']."""
    target: str = data.get("targetPeerId", "")
    if not target:
        log.warning("[%s] sid=%s – missing targetPeerId", tag, sid)
        return
    data.setdefault("fromPeerId", sid)
    await sio.emit(event, data, to=target)
    log.debug("[%s]  %s → %s", tag, sid, target)


@sio.on("file-offer")
async def on_file_offer(sid: str, data: dict) -> dict:
    """
    Payload: { targetPeerId, transferId, name, size, type,
               totalChunks, senderId }
    Stores the transfer so file-accept can route back without targetPeerId.
    """
    transfer_id: str = data.get("transferId", "")
    target:      str = data.get("targetPeerId", "")

    if transfer_id and target:
        transfers[transfer_id] = {"senderId": sid, "receiverId": target}

    data.setdefault("senderId", sid)
    data.setdefault("fromPeerId", sid)
    await sio.emit("file-offer", data, to=target)
    log.info("[FILE-OFFER]  %s → %s  transferId=%s", sid, target, transfer_id)
    return _ok()


@sio.on("file-accept")
async def on_file_accept(sid: str, data: dict) -> dict:
    """
    Payload: { transferId }
    Looks up the original sender and relays 'file-accepted' to them.
    """
    transfer_id: str = data.get("transferId", "")
    transfer         = transfers.get(transfer_id)

    if not transfer:
        log.warning("[FILE-ACCEPT] unknown transferId=%s  sid=%s",
                    transfer_id, sid)
        return _err(f"Unknown transferId: {transfer_id}")

    sender_id: str = transfer["senderId"]
    await sio.emit(
        "file-accepted",
        {"transferId": transfer_id, "fromPeerId": sid},
        to=sender_id,
    )
    log.info("[FILE-ACCEPT]  receiver=%s → sender=%s  transferId=%s",
             sid, sender_id, transfer_id)
    return _ok()


@sio.on("file-reject")
async def on_file_reject(sid: str, data: dict) -> dict:
    """
    Payload: { transferId }
    """
    transfer_id: str = data.get("transferId", "")
    transfer         = transfers.pop(transfer_id, None)

    if transfer:
        await sio.emit(
            "file-rejected",
            {"transferId": transfer_id, "fromPeerId": sid},
            to=transfer["senderId"],
        )
        log.info("[FILE-REJECT]  %s  transferId=%s", sid, transfer_id)
    return _ok()


@sio.on("file-cancel")
async def on_file_cancel(sid: str, data: dict) -> dict:
    """
    Payload: { transferId }
    """
    transfer_id: str = data.get("transferId", "")
    transfer         = transfers.pop(transfer_id, None)

    if transfer:
        # Notify whichever peer did NOT send the cancel
        other = (transfer["senderId"]
                 if sid == transfer["receiverId"]
                 else transfer["receiverId"])
        await sio.emit(
            "file-cancelled",
            {"transferId": transfer_id, "fromPeerId": sid},
            to=other,
        )
        log.info("[FILE-CANCEL]  %s  transferId=%s", sid, transfer_id)
    return _ok()


@sio.on("file-progress")
async def on_file_progress(sid: str, data: dict) -> None:
    """Payload: { transferId, progress, chunksReceived }  – relay to sender."""
    transfer_id: str = data.get("transferId", "")
    transfer         = transfers.get(transfer_id)
    if transfer:
        await sio.emit("file-progress", data,
                       to=transfer["senderId"])


@sio.on("file-complete")
async def on_file_complete(sid: str, data: dict) -> None:
    """Payload: { transferId }  – relay to sender."""
    transfer_id: str = data.get("transferId", "")
    transfer         = transfers.pop(transfer_id, None)
    if transfer:
        await sio.emit("file-complete", data,
                       to=transfer["senderId"])
        log.info("[FILE-COMPLETE]  transferId=%s", transfer_id)


# ---------------------------------------------------------------------------
# Socket.IO – WebRTC signalling relay
# ---------------------------------------------------------------------------

@sio.on("webrtc-offer")
async def on_webrtc_offer(sid: str, data: dict) -> dict:
    """
    Payload: { targetPeerId, transferId, offer: { type, sdp } }
    """
    target: str = data.get("targetPeerId", "")
    if not target:
        log.warning("[WEBRTC-OFFER] sid=%s – missing targetPeerId", sid)
        return _err("targetPeerId is required")

    data.setdefault("fromPeerId", sid)
    await sio.emit("webrtc-offer", data, to=target)
    log.debug("[WEBRTC-OFFER]  %s → %s  transferId=%s",
              sid, target, data.get("transferId"))
    return _ok()


@sio.on("webrtc-answer")
async def on_webrtc_answer(sid: str, data: dict) -> dict:
    """
    Payload: { targetPeerId, transferId, answer: { type, sdp } }
    """
    target: str = data.get("targetPeerId", "")
    if not target:
        log.warning("[WEBRTC-ANSWER] sid=%s – missing targetPeerId", sid)
        return _err("targetPeerId is required")

    data.setdefault("fromPeerId", sid)
    await sio.emit("webrtc-answer", data, to=target)
    log.debug("[WEBRTC-ANSWER]  %s → %s  transferId=%s",
              sid, target, data.get("transferId"))
    return _ok()


@sio.on("webrtc-ice-candidate")
async def on_webrtc_ice_candidate(sid: str, data: dict) -> None:
    """
    Payload: { targetPeerId, transferId, candidate: { candidate, sdpMid, … } }
    """
    await _relay("webrtc-ice-candidate", sid, data, "ICE-CANDIDATE")


# ---------------------------------------------------------------------------
# Entrypoint
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    HOST = "0.0.0.0"
    PORT = 3000

    log.info("File Transfer signalling server starting on %s:%d", HOST, PORT)
    log.info("HTTP endpoints:")
    log.info("  POST /api/v1/create-room  { password }")
    log.info("  POST /api/v1/login        { roomId, password }")

    aiohttp.web.run_app(app, host=HOST, port=PORT, print=None)