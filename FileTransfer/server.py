#!/usr/bin/env python3
"""
File Transfer Signaling Server
- Manages room-based peer connections
- Exchanges WebRTC offers/answers and ICE candidates
- Handles authentication
"""

import asyncio
import uuid
import logging
from datetime import datetime
from typing import Dict, Set
from dataclasses import dataclass
from aiohttp import web
from socketio import AsyncServer

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger("FileTransferServer")

@dataclass
class Peer:
    id: str
    sid: str
    room_id: str
    username: str = "Anonymous"
    joined_at: str = ""
    
    def __post_init__(self):
        if not self.joined_at:
            self.joined_at = datetime.now().isoformat()

class FileTransferServer:
    def __init__(self, host: str = "0.0.0.0", port: int = 3000):
        self.host = host
        self.port = port
        self.sio = AsyncServer(async_mode='aiohttp', cors_allowed_origins='*')
        self.app = web.Application()
        self.sio.attach(self.app)
        
        self.peers: Dict[str, Dict[str, Peer]] = {}
        self.peer_sids: Dict[str, str] = {}
        self.sid_to_room: Dict[str, str] = {}
        self.authenticated_users: Set[str] = set()
        
        self._setup_routes()
        self._setup_socket_handlers()
        
    def _setup_routes(self):
        self.app.router.add_get('/', self._index_handler)
        self.app.router.add_get('/health', self._health_handler)
        self.app.router.add_post('/login', self._login_handler)
        
    def _setup_socket_handlers(self):
        @self.sio.event
        async def connect(sid, environ):
            logger.info(f"[CONNECT] Client connected: {sid}")
            return True
            
        @self.sio.event
        async def disconnect(sid):
            room_id = self.sid_to_room.get(sid)
            if room_id and room_id in self.peers:
                peers_in_room = self.peers[room_id]
                if sid in peers_in_room:
                    peer = peers_in_room[sid]
                    logger.info(f"[DISCONNECT] Peer {peer.id} left room {room_id}")
                    await self.sio.emit('peer-left', {'peerId': peer.id},
                                        room=room_id, skip_sid=sid)
                    del peers_in_room[sid]
                    self.peer_sids.pop(peer.id, None)
            self.sid_to_room.pop(sid, None)
            self.authenticated_users.discard(sid)
                
        @self.sio.event
        async def login(sid, data):
            logger.info(f"[LOGIN] User attempting to login: {data}")
            room_id = data.get('roomId', 'default-room')
            password = data.get('password', '')
            username = data.get('username', 'User')
            
            if password != 'password':
                await self.sio.emit('login-failed', {'reason': 'Invalid password'}, to=sid)
                return
            
            peer_id = str(uuid.uuid4())[:8]
            peer = Peer(id=peer_id, sid=sid, room_id=room_id, username=username)
            
            if room_id not in self.peers:
                self.peers[room_id] = {}
            self.peers[room_id][sid] = peer
            self.peer_sids[peer_id] = sid
            self.sid_to_room[sid] = room_id
            self.authenticated_users.add(sid)
            
            existing_peers = [
                {'id': p.id, 'username': p.username, 'joinedAt': p.joined_at}
                for p in self.peers[room_id].values() if p.sid != sid
            ]
            
            await self.sio.emit('login-success',
                                {'userId': peer_id, 'roomId': room_id, 'peers': existing_peers},
                                to=sid)
            await self.sio.emit('peer-joined',
                                {'peerId': peer_id, 'username': username, 'joinedAt': peer.joined_at},
                                room=room_id, skip_sid=sid)
            logger.info(f"[LOGIN] Peer {peer_id} joined room {room_id}")
            
        @self.sio.event
        async def join_room(sid, data):
            if sid not in self.authenticated_users:
                return {'success': False, 'reason': 'Not authenticated'}

            room_id = data.get('roomId')
            if not room_id or room_id not in self.peers:
                return {'success': False, 'reason': 'Invalid room'}

            peers = [
                {'id': p.id, 'username': p.username, 'joinedAt': p.joined_at}
                for p in self.peers[room_id].values()
            ]

            # Emit joined event for compatibility
            await self.sio.emit('joined', {'roomId': room_id, 'peers': peers}, to=sid)
            logger.info(f"[JOIN] Peer joined room {room_id}")

            # Return dict so client ACK callback fires
            return {'success': True, 'roomId': room_id, 'peers': peers}
        
        # send_offer, send_answer, ice_candidate unchanged
        
    async def _index_handler(self, request):
        return web.json_response({
            'status': 'ok',
            'message': 'File Transfer Signaling Server',
            'rooms': list(self.peers.keys()),
            'total_peers': sum(len(peers) for peers in self.peers.values())
        })
        
    async def _health_handler(self, request):
        return web.json_response({'status': 'healthy'})
        
    async def _login_handler(self, request):
        try:
            data = await request.json()
            room_id = data.get('roomId', 'default-room')
            password = data.get('password', '')
            username = data.get('username', 'User')
            
            if password != 'password':
                return web.json_response({'success': False, 'reason': 'Invalid password'}, status=401)
            
            peer_id = str(uuid.uuid4())[:8]
            return web.json_response({
                'success': True,
                'peerId': peer_id,
                'roomId': room_id,
                'cookie': f'peer_{peer_id}'
            })
        except Exception as e:
            logger.error(f"Login error: {e}")
            return web.json_response({'success': False, 'reason': str(e)}, status=400)
        
    async def start(self):
        runner = web.AppRunner(self.app)
        await runner.setup()
        site = web.TCPSite(runner, self.host, self.port)
        await site.start()
        logger.info(f"[SERVER] Started on {self.host}:{self.port}")
        logger.info(f"[SERVER] WebSocket endpoint: ws://{self.host}:{self.port}/socket.io/")
        try:
            await asyncio.Future()
        except KeyboardInterrupt:
            await runner.cleanup()

async def main():
    server = FileTransferServer(host='127.0.0.1', port=3000)
    await server.start()

if __name__ == '__main__':
    asyncio.run(main())
