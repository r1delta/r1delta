@0xd828d59fef957118;

struct Player {
  name @0 :Text;
  gen @1 :Int32;
  lvl @2 :Int32;
  team @3 :Int32;
}

struct ServerHeartbeat {
  hostname @0 :Text;
  mapName @1 :Text;
  gameMode @2 :Text;
  players @3 :List(Player);
  maxPlayers @4 :Int32;
  port @5 :Int32;
  ip @6 :Text;
}

struct ServerList {
  servers @0 :List(ServerHeartbeat);
}
