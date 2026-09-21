#include <stdint.h>
#include <stdio.h>

#include "s2c.h"
#include "enums.h"
#include "log.h"
#include "encryption.h"
#include "wrapper.h"
#include "util.h"
#include "blocks.h"
#include "mc_registry_names.h"
#include "mc_tag_names.h"

#ifdef ONLINE_MODE
#include "mbedtls/base64.h"
#endif /*ONLINE_MODE*/

/* 26.2(协议 776) 起：Login Success 追加 Session ID；包内文本组件由 JSON 改为
 * “无根名” NBT 字符串。 */
#define MC_PROTO_SESSION_ID 776
#define MC_PROTO_NBT_COMPONENT 776

/* 以当前协议版本发送一个文本组件（>=776 用 NBT，否则用 JSON 字符串）。 */
static void sendTextComponent(player_t *player, const char *text)
{
  if ((player != NULL) && (player->protocol >= MC_PROTO_NBT_COMPONENT))
  {
    size_t n = strlen(text);
    sendByte(0x08); /* TAG_String（无根名） */
    sendByte((uint8_t)(n >> 8));
    sendByte((uint8_t)(n & 0xFF));
    sendBuffer(text, n);
  }
  else
  {
    char buf[300];
    size_t l = (size_t)snprintf(buf, sizeof(buf), "{\"text\":\"%s\"}", text);
    sendString(buf, l);
  }
}

// Status packets
void StatusS2Cresponse(player_t *currentPlayer)
{
  char buffer[300];
  char scratch[24];
  static const char json1[] = "{\"description\":\"" MOTD "\", \"players\":{\"max\":";
  static const char json2[] = ",\"online\":";
  static const char json3[] = "},\"version\":{\"name\":\"" LONG_PROTOCOL_VERSION "\",\"protocol\":";
  static const char json4[] = "}}";
  sendStart();
  sendByte(0);
  sendSwitchToLocalBuffer(buffer, sizeof(buffer));
  sendBuffer(json1, strlen(json1));
  snprintf(scratch, sizeof(scratch), "%d", MAX_PLAYERS);
  sendBuffer(scratch, strnlen(scratch, sizeof(scratch)));
  sendBuffer(json2, strlen(json2));
  snprintf(scratch, sizeof(scratch), "%ld", playerGetActiveCount());
  sendBuffer(scratch, strnlen(scratch, sizeof(scratch)));
  sendBuffer(json3, strlen(json3));
  snprintf(scratch, sizeof(scratch), "%d", currentPlayer->protocol ? currentPlayer->protocol : PROTOCOL_VERSION);
  sendBuffer(scratch, strnlen(scratch, sizeof(scratch)));
  sendBuffer(json4, strlen(json4));
  int size = sendRevertFromLocalBuffer();
  sendString(buffer, size);
  sendDone();
}
void StatusS2Cpong(player_t *currentPlayer)
{

  sendStart();
  sendByte(0x01);
  sendBuffer(currentPlayer->name, 8);
  sendDone();
}
// Login packets
void LoginS2Cdisconnect(player_t *currentPlayer, char *reason)
{
  char buffer[300];
  sendStart();
  sendByte(0x00);
  size_t len = snprintf(buffer, sizeof(buffer), "{\"text\":\"%s\"}", reason);
  sendString(buffer, len);
  sendDone();
}
#ifdef ONLINE_MODE
void LoginS2Cencryptionrequest(player_t *currentPlayer)
{
  uint8_t key_buf[256];
  char serverid[20];
  char token[4];
  sendStart();
  sendByte(0x01);
  // send the server id {scratch buffer}
  memset(serverid, 0, sizeof(serverid));
  sendString((char *)serverid, 20);
  int ret = mbedtls_pk_write_pubkey_der(&encryptionGetData()->key, key_buf, sizeof(key_buf));
  if (ret < 0)
  {
    printl(LOG_ERROR, "mbedtls_pk_write_pubkey_der returned %d\n", ret);
    return;
  }
  // send the public key
  sendVarInt(ret);
  sendBuffer((char *)&key_buf[sizeof(key_buf) - ret], ret);
  // generate a random token
  ret = mbedtls_ctr_drbg_random(&encryptionGetData()->ctr_drbg, (unsigned char *)&token, sizeof(token));
  if (ret < 0)
  {
    printl(LOG_ERROR, "mbedtls_ctr_drbg_random returned %d\n", ret);
    return;
  }
  memcpy(currentPlayer->verify_token, token, sizeof(token));
  sendVarInt(4);
  sendBuffer(token, 4);
  sendByte(1);
  sendDone();
}
#endif /*ONLINE_MODE*/
void LoginS2Ccompression(player_t *currentPlayer)
{
  sendStart();
  sendByte(0x3);
  sendVarInt(COMPRESSION_THRESHOLD);
  sendDone();
  currentPlayer->compression_flag = 1;
}
void LoginS2Csuccess(player_t *currentPlayer)
{
  sendStart();
  sendByte(0x02);
  sendUUID(currentPlayer->id); // UUID
  // sendBuffer((char*)currentPlayer->uuid, 16);
  sendString(currentPlayer->name, -1); // Username
#ifdef ONLINE_MODE_AUTH
  if (currentPlayer->texture_value && currentPlayer->texture_signature)
  {
    sendVarInt(1);                                                              // Number Of Properties
    sendString("textures", -1);                                                 // Property Name
    sendString(currentPlayer->texture_value, currentPlayer->texture_value_len); // Value
    sendByte(1);                                                                // Is Signed
    sendString(currentPlayer->texture_signature, currentPlayer->texture_signature_len);
  }
  else
  {
    sendVarInt(0); // Number Of Properties
  }
#else
  sendVarInt(0); // Number Of Properties
#endif /*ONLINE_MODE_AUTH*/
  if (currentPlayer->protocol >= MC_PROTO_SESSION_ID)
  {
    uint8_t k;
    for (k = 0; k < 16; k++)
    {
      sendByte(0x00); // Session ID (UUID)，离线演示用全 0
    }
  }
  sendDone();
}
// Play packets

void PlayS2Clogin(player_t *currentPlayer)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_LOGIN);
  sendInt(currentPlayer->id);
  sendByte(0);                 // Is hardcore
  sendVarInt(1);               // World Count
  sendString("overworld", -1); // Dimension Names
  sendVarInt(MAX_PLAYERS);     // Max Players
  sendVarInt(VIEWDISTANCE);    // viewdistance
  sendVarInt(VIEWDISTANCE);    // simulationdistance
  sendByte(0);                 // Reduced Debug Info
  sendByte(1);                 // Enable respawn screen
  sendByte(0);                 // Do limited crafting
  sendVarInt(0);               // Dimension Type
  sendString("overworld", -1); // Dimension Name
  sendLong(0x482304890);       //{hashedSeed} random bytes
  sendByte(GAMEMODE);          // gamemode
  currentPlayer->gamemode = GAMEMODE;
  sendByte(-1);   // previous gamemode
  sendByte(0);    // Is Debug
  sendByte(0);    // Is Flat
  sendByte(0);    // Has death location
  sendVarInt(0);  // Portal cooldown
  sendVarInt(64); // Sea level
  sendByte(0);    // Enforces Secure Chat
  sendDone();
}
void PlayS2Ctablist(player_t *currentPlayer, TabListAction action, uint16_t eid)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_PLAYER_INFO_UPDATE);
  // TODO: handle more actions
  sendByte(action);
  sendVarInt(1); // Number Of Actions
  sendUUID(eid);
  if (action & TABLIST_ACTION_ADDPLAYER)
  {
    sendString(currentPlayer->name, -1);
#ifdef ONLINE_MODE_AUTH
    if (currentPlayer->texture_value && currentPlayer->texture_signature)
    {
      sendVarInt(1);                                                              // Number Of Properties
      sendString("textures", -1);                                                 // Property Name
      sendString(currentPlayer->texture_value, currentPlayer->texture_value_len); // Value
      sendByte(1);                                                                // Is Signed
      sendString(currentPlayer->texture_signature, currentPlayer->texture_signature_len);
    }
    else
    {
      sendVarInt(0); // Number Of Properties
    }
#else
    sendVarInt(0); // Number Of Properties
#endif /*ONLINE_MODE_AUTH*/
  }
  if (action & TABLIST_ACTION_GAMEMODE)
  {
    sendVarInt(currentPlayer->gamemode);
  }
  if (action & TABLIST_ACTION_LISTED)
  {
    sendByte(1);
  }
  if (action & TABLIST_ACTION_LATENCY)
  {
    sendVarInt(0);
  }
  if (action & TABLIST_ACTION_NAME)
  {
    printl(LOG_WARN, "tablist action name is not implemented!\n");
    sendByte(0x0);
  }
  sendDone();
}
void PlayS2Cgameevent(GameEvent event, float value)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_GAME_EVENT);
  sendByte(event);
  sendFloat(value);
  sendDone();
}
void PlayS2Cplayerabilities(player_t *currentPlayer, PlayerAbilities abilities)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_PLAYER_ABILITIES);
  sendByte(abilities);
  sendFloat(0.05); // Flying Speed
  sendFloat(0.1);  // FOV Modifier
  sendDone();
}
void PlayS2Ctablistremove(uint16_t eid)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_PLAYER_INFO_REMOVE);
  sendVarInt(1);
  sendUUID(eid);
  sendDone();
}
void PlayS2Cspawnentity(player_t *currentPlayer, EntityMetadataType type)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_ADD_ENTITY);
  sendVarInt(currentPlayer->id); // EID
  sendUUID(currentPlayer->id);   // UUID
  sendVarInt(type);              // Entity Type
  sendDouble(0);
  sendDouble(0);
  sendDouble(0);
  sendByte(0);
  sendByte(currentPlayer->npitch);
  sendByte(currentPlayer->nyaw);
  sendByte(currentPlayer->nyaw);
  sendVarInt(0);
  sendDone();
}
void PlayS2Cpositionrotation(player_t *currentPlayer, double x, double y, double z)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_PLAYER_POSITION);
  sendVarInt(0); // teleportId
  sendDouble(x);
  sendDouble(y);
  sendDouble(z);
  // TODO: velocity fields, unknown for now
  sendDouble(0); // x
  sendDouble(0); // y
  sendDouble(0); // z
  sendFloat(currentPlayer->yaw);
  sendFloat(currentPlayer->pitch);
  sendInt(0); // xyz absolute
  sendDone();
}
void PlayS2Cchunkcenter(player_t *currentPlayer, int32_t x, int32_t z)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_SET_CHUNK_CACHE_CENTER);
  sendVarInt(x);
  sendVarInt(z);
  sendDone();
}
void PlayS2Cchunk(player_t *currentPlayer, int32_t x, int32_t z, int32_t from, int32_t to)
{

  sendStart();
  sendPlayPacketHeader(S2C_PLAY_LEVEL_CHUNK_WITH_LIGHT);
  sendInt(x);
  sendInt(z);
  sendVarInt(0); // Heightmap count
  sendPrefixedStart();
  worldGenerateChunk(x, z, from, to);
  sendPrefixedEnd();
  sendVarInt(0); // block entiies
  sendVarInt(1); // Sky Light Mask
  const int light_section_count = to + 1;
  const uint64_t light_mask = (light_section_count >= 64) ? UINT64_MAX : ((1ULL << light_section_count) - 1);
  sendLong((int64_t)light_mask);
  // Block Light Mask
  sendVarInt(1);
  sendLong(0);
  // Empty Sky Light Mask
  sendVarInt(1);
  sendLong(0);
  // Empty Block Light Mask
  sendVarInt(1);
  sendLong((int64_t)light_mask);

  sendVarInt(light_section_count); // Sky Light array count
  for (int i = 0; i < light_section_count; i++)
  {
    sendVarInt(2048);
    for (int b = 0; b < 2048; b++)
    {
      sendByte(0xFF);
    }
  }
  sendVarInt(0); // Block Light array count
  sendDone();

  Blocks *blocks = blocksGet(x, z);
  if (blocks != NULL)
  {
    for (size_t i = 0; i < blocks->count; i++)
    {
      PlayS2Cblock(blocks->block[i].default_state, blocks->block[i].c.x + (x << 4), blocks->block[i].c.y, blocks->block[i].c.z + (z << 4));
    }
  }
}
void PlayS2Cheartbeat(player_t *currentPlayer)
{
  extern size_t main_tick;
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_KEEP_ALIVE);
  sendLong(main_tick);
  sendDone();
}
void PlayS2Crotation(player_t *currentPlayer)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_MOVE_ENTITY_ROT);
  sendVarInt(currentPlayer->id);
  sendByte(currentPlayer->nyaw);
  sendByte(currentPlayer->npitch);
  sendByte(currentPlayer->onground);
  sendDone();
}
void PlayS2Cteleport(player_t *currentPlayer, double x, double y, double z)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_ENTITY_POSITION_SYNC);
  sendVarInt(currentPlayer->id);
  sendDouble(x);
  sendDouble(y);
  sendDouble(z);
  // TODO: Velocity fields
  sendDouble(0);
  sendDouble(0);
  sendDouble(0);
  sendFloat(currentPlayer->yaw);
  sendFloat(currentPlayer->pitch);
  sendByte(currentPlayer->onground);
  sendDone();
  currentPlayer->x = x;
  currentPlayer->y = y;
  currentPlayer->z = z;
}
void PlayS2Cheadrotation(player_t *currentPlayer)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_ROTATE_HEAD);
  sendVarInt(currentPlayer->id);
  sendByte(currentPlayer->nyaw);
  sendDone();
}
void PlayS2Centityanimation(player_t *currentPlayer, uint8_t animation)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_ANIMATE);
  sendVarInt(currentPlayer->id);
  sendByte(animation);
  sendDone();
}

void PlayS2Csysmessage(char *message, size_t len)
{
  (void)len;
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_SYSTEM_CHAT);
  sendTextComponent(sendGetPlayer(), message);
  sendByte(0);
  sendDone();
}
void PlayS2Centitydestroy(int32_t eid)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_REMOVE_ENTITIES);
  sendVarInt(1);
  sendVarInt(eid);
  sendDone();
}
void PlayS2Cblock(blocksDefaultState blockstate, int32_t x, int32_t y, int32_t z)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_BLOCK_UPDATE);
  sendPosition(x, y, z);
  sendVarInt(blockstate);
  sendDone();
}
// Acknowledge Block Change
void PlayS2Cblockchangeack(player_t *currentPlayer, int32_t sequence)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_BLOCK_CHANGED_ACK);
  sendVarInt(sequence);
  sendDone();
}
// TODO: make this packet more cleaner
void PlayS2Cbossbar(player_t *currentPlayer, uint16_t uuid, int32_t action, char *title, size_t len, float health)
{
  char buf[300];
  size_t size;
  static const char json1[] = "{\"text\":\"";
  static const char json2[] = "\"}";
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_BOSS_EVENT);
  sendUUID(uuid);
  sendVarInt(action);
  switch (action)
  {
  case 0:
    sendSwitchToLocalBuffer(buf, sizeof(buf));
    sendBuffer(json1, strlen(json1));
    if (len < sizeof(buf) - 13)
    {
      if (title)
      {
        sendBuffer(title, len);
      }
    }
    sendBuffer(json2, strlen(json2));
    size = sendRevertFromLocalBuffer();
    sendString(buf, size);
    sendFloat(health);
    sendVarInt(4);
    sendVarInt(0); // 20 notches
    sendByte(0);
    break;
  case 2:
    sendFloat(health);
    break;
  case 3:
    sendSwitchToLocalBuffer(buf, sizeof(buf));
    sendBuffer(json1, strlen(json1));
    if (len < sizeof(buf) - 13)
    {
      if (title)
      {
        sendBuffer(title, len);
      }
    }
    sendBuffer(json2, strlen(json2));
    size = sendRevertFromLocalBuffer();
    sendString(buf, size);
  default:
    break;
  }
  sendDone();
}
void PlayS2Centitydata(player_t *currentPlayer, uint8_t entity, EntityDataMetadata type, EntityState state)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_SET_ENTITY_DATA);
  sendVarInt(currentPlayer->id);
  sendByte(entity); // Entity base class "Pose" field ENTITY_POSE
  sendVarInt(type); // Metadata
  sendVarInt(state);
  sendByte(0xff);
  sendDone();
}
void PlayS2Ccompassposition(player_t *currentPlayer, int32_t x, int32_t y, int32_t z)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_SET_DEFAULT_SPAWN_POSITION);
  sendString("overworld", -1);
  sendPosition(x, y, z);
  sendFloat(0);
  sendFloat(0);
  sendDone();
}
void PlayS2Cdisconnect(player_t *currentPlayer, char *reason)
{
  size_t len = strnlen(reason, sizeof(((player_t *)0)->disconnect_reason));
  (void)len;
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_DISCONNECT);
  sendTextComponent(currentPlayer, reason);
  sendDone();
}
void PlayS2Csettime(int64_t time_of_day, uint8_t time_of_day_increasing)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_SET_TIME);
  sendLong(time_of_day);
  sendByte(time_of_day_increasing ? 1 : 0);
  sendDone();
}

void PlayS2Ccontainersetcontent(player_t *currentPlayer, storage_t *inventory)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_CONTAINER_SET_CONTENT);
  sendVarInt(0); // Window ID
  sendVarInt(0); // State ID
  sendVarInt(INVENTORY_SIZE);
  for (int i = 0; i < INVENTORY_SIZE; i++)
  {
    sendVarInt(inventory->inventory_slots[i].count); // Item count
    if (inventory->inventory_slots[i].count != 0)
    {
      sendVarInt(inventory->inventory_slots[i].item_id); // Item ID
      sendVarInt(0);                                     // Add Data component array
      sendVarInt(0);                                     // Remove  Data component array
    }
  }
  sendVarInt(0); // Dragged by mouse Item Count
  sendDone();
}

void PlayS2Ccontainersetslot(player_t *currentPlayer, int32_t window_id, int16_t slot, int16_t count, int32_t item_id)
{
  sendStart();
  sendPlayPacketHeader(S2C_PLAY_CONTAINER_SET_SLOT);
  sendVarInt(window_id); // window id
  sendVarInt(0);         // State ID
  sendShort(slot);       // Slot
  sendVarInt(count);     // Item count
  if (count)
  {
    sendVarInt(item_id);
    sendVarInt(0);
    sendVarInt(0);
  }
  sendDone();
}

void PlayS2Copenscreen(player_t *currentPlayer, int32_t window_id, int32_t window_type, char *window_title)
{

  sendStart();
  sendPlayPacketHeader(S2C_PLAY_OPEN_SCREEN);
  sendVarInt(window_id);
  sendVarInt(window_type);
  sendFormattedString(window_title, -1);
  sendDone();
}
void ConfigurationS2Cfeatures()
{
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_UPDATE_ENABLED_FEATURES);
  sendVarInt(1);
  sendString("vanilla", -1);
  sendDone();
}
void ConfigurationS2Cknownpacks()
{
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_SELECT_KNOWN_PACKS);
  sendVarInt(1);
  sendString("minecraft", -1);
  sendString("core", -1);
  sendString(CLIENT_VERSION, -1);
  sendDone();
}

static int cfg_phase = 0;      /* 0=注册表 1=标签 2=完成 */
static uint32_t cfg_reg = 0;    /* 已发注册表数 */
static uint32_t cfg_tag_g = 0;  /* 已发标签组 */
static uint32_t cfg_tag_i = 0;  /* 组内已发标签数 */

void ConfigurationS2Creset(void)
{
  cfg_phase = 0;
  cfg_reg = 0;
  cfg_tag_g = 0;
  cfg_tag_i = 0;
}

int ConfigurationS2Cprogress(void)
{
  enum { CFG_PKTS_PER_TICK = 4 };
  int budget = CFG_PKTS_PER_TICK;
  if (cfg_phase == 0)
  {
    while (cfg_reg < MC_REGISTRY_COUNT && budget > 0)
    {
      const mc_registry_desc_t *reg = &mc_registries[cfg_reg];
      sendStart();
      sendConfigurationPacketHeader(S2C_CONFIGURATION_REGISTRY_DATA);
      sendString(reg->registry, -1);
      sendVarInt((int32_t)reg->count);
      for (uint16_t i = 0; i < reg->count; i++)
      {
        sendString(reg->names[i], -1);
        sendByte(0);
      }
      sendDone();
      sendDispatch();
      cfg_reg++;
      budget--;
    }
    if (cfg_reg >= MC_REGISTRY_COUNT)
      cfg_phase = 1;
  }
  if (cfg_phase == 1)
  {
    while (cfg_tag_g < MC_TAG_GROUP_COUNT && budget > 0)
    {
      /* 每个注册表的标签必须在同一个 Update Tags 包内一次发完：
       * 同一注册表分多包时后者会覆盖前者（只剩最后一包）。 */
      const mc_tag_group_t *grp = &mc_tag_groups[cfg_tag_g];
      sendStart();
      sendConfigurationPacketHeader(S2C_CONFIGURATION_UPDATE_TAGS);
      sendByte(1);
      sendString(grp->registry, -1);
      sendVarInt(grp->count);
      for (uint16_t j = 0; j < grp->count; j++)
      {
        sendString(grp->tags[j], -1);
        sendByte(0);
      }
      sendDone();
      sendDispatch();
      cfg_tag_g++;
      budget--;
    }
    if (cfg_tag_g >= MC_TAG_GROUP_COUNT)
      cfg_phase = 2;
  }
  return cfg_phase == 2;
}
void ConfigurationS2Cready()
{
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_FINISH_CONFIGURATION);
  sendDone();
}
void ConfigurationS2Cdisconnect(player_t *currentPlayer, char *reason)
{
  sendStart();
  sendConfigurationPacketHeader(S2C_CONFIGURATION_DISCONNECT);
  sendTextComponent(currentPlayer, reason);
  sendDone();
}
