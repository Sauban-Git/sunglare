#include <cstdint>
#include <mod/amlmod.h>
#include <mod/config.h>
#include <mod/logger.h>

// -----------------------------------------------------------------------------
// Architecture-Specific Offsets
// -----------------------------------------------------------------------------
#if defined(__aarch64__)
MYMODCFGNAME(net.retro.gtavsunglare64, GTAV Custom Sun Glare(64 - bit), 2.0,
             Retro, GTAVSunGlare64)

constexpr uintptr_t OFFSET_CUSTOM_PIPE_MAT_SETUP = 0x38CCAC;
constexpr uintptr_t OFFSET_ENV_MAP_PLUGIN_OFFSET = 0x85EBC0;
constexpr uintptr_t OFFSET_RW_TEXTURE_READ = 0x1E4AA0;

// CTxdStore API Addresses (64-bit)
constexpr uintptr_t OFFSET_CTXDSTORE_FIND_SLOT = 0x2F7814;
constexpr uintptr_t OFFSET_CTXDSTORE_PUSH_TXD = 0x2F78A8;
constexpr uintptr_t OFFSET_CTXDSTORE_SET_TXD = 0x2F785C;
constexpr uintptr_t OFFSET_CTXDSTORE_POP_TXD = 0x2F78E4;

#elif defined(__arm__) || defined(__i386__)
MYMODCFGNAME(net.retro.gtavsunglare, GTAV Custom Sun Glare(32 - bit), 2.0,
             Retro, GTAVSunGlare)

constexpr uintptr_t OFFSET_CUSTOM_PIPE_MAT_SETUP = 0x2CB575;
constexpr uintptr_t OFFSET_ENV_MAP_PLUGIN_OFFSET = 0x685F88;
constexpr uintptr_t OFFSET_RW_TEXTURE_READ = 0x1B5A0C;

// CTxdStore API Addresses (32-bit Thumb +1)
constexpr uintptr_t OFFSET_CTXDSTORE_FIND_SLOT = 0x221EC1;
constexpr uintptr_t OFFSET_CTXDSTORE_PUSH_TXD = 0x221F55;
constexpr uintptr_t OFFSET_CTXDSTORE_SET_TXD = 0x221F09;
constexpr uintptr_t OFFSET_CTXDSTORE_POP_TXD = 0x221F91;

#else
#error "Unsupported Architecture!"
#endif

NEEDGAME(com.rockstargames.gtasa)

BEGIN_DEPLIST()
ADD_DEPENDENCY_VER(net.rusjj.aml, 1.4.0)
END_DEPLIST()

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------
typedef void *(*RwTextureRead_t)(const char *name, const char *maskName);
typedef int (*FindTxdSlot_t)(const char *name);
typedef void (*PushCurrentTxd_t)();
typedef void (*SetCurrentTxd_t)(int slot);
typedef void (*PopCurrentTxd_t)();

RwTextureRead_t RwTextureRead = nullptr;
FindTxdSlot_t FindTxdSlot = nullptr;
PushCurrentTxd_t PushCurrentTxd = nullptr;
SetCurrentTxd_t SetCurrentTxd = nullptr;
PopCurrentTxd_t PopCurrentTxd = nullptr;

uintptr_t pGTASA = 0;
int32_t *g_pEnvMapPluginOffset = nullptr;

// Custom Glare Texture Pointer
void *g_pSunGlareTexture = nullptr;

bool IsValidVehicleMaterial(void *pMaterial) {
  if (!pMaterial || !g_pEnvMapPluginOffset || *g_pEnvMapPluginOffset <= 0)
    return false;
  uintptr_t matAddr = reinterpret_cast<uintptr_t>(pMaterial);
  void *pluginData = *reinterpret_cast<void **>(
      matAddr + static_cast<uintptr_t>(*g_pEnvMapPluginOffset));
  return (pluginData != nullptr);
}

// -----------------------------------------------------------------------------
// Material Setup Hook: Swaps Reflection Texture to glaresun.png
// -----------------------------------------------------------------------------
DECL_HOOKv(CustomPipeMaterialSetup, void *pMaterial, void *pData) {
  void *pOriginalTexture = nullptr;
  uintptr_t envMapDataAddr = 0;

  if (g_pSunGlareTexture && IsValidVehicleMaterial(pMaterial)) {
    uintptr_t matAddr = reinterpret_cast<uintptr_t>(pMaterial);
    int32_t offset = *g_pEnvMapPluginOffset;
    envMapDataAddr = *reinterpret_cast<uintptr_t *>(
        matAddr + static_cast<uintptr_t>(offset));

    if (envMapDataAddr) {
      // Cache original texture pointer (offset 0 inside RpEnvMapPlugin
      // structure)
      pOriginalTexture = *reinterpret_cast<void **>(envMapDataAddr);

      // Inject glaresun.png as the active environmental reflection texture
      *reinterpret_cast<void **>(envMapDataAddr) = g_pSunGlareTexture;
    }
  }

  // Run standard material setup pass (GPU binds glaresun.png to reflection
  // sampler)
  CustomPipeMaterialSetup(pMaterial, pData);

  // Restore original reflection map (prevents skybox/enviroment map corruption)
  if (envMapDataAddr && pOriginalTexture) {
    *reinterpret_cast<void **>(envMapDataAddr) = pOriginalTexture;
  }
}

// Helper to safely fetch texture from the gta3 TXD container
void LoadGlareTexture() {
  if (!FindTxdSlot || !SetCurrentTxd || !PushCurrentTxd || !PopCurrentTxd ||
      !RwTextureRead)
    return;

  int slot = FindTxdSlot("gta3");
  if (slot != -1) {
    PushCurrentTxd();
    SetCurrentTxd(slot);

    // Load glaresun.png (without alpha mask)
    g_pSunGlareTexture = RwTextureRead("glaresun", nullptr);

    PopCurrentTxd();

    if (g_pSunGlareTexture) {
      logger->Info("Successfully loaded glaresun from gta3 texdb!");
    } else {
      logger->Error("Failed to find glaresun in gta3 texdb!");
    }
  } else {
    logger->Error("Could not locate gta3 TXD slot!");
  }
}

ON_MOD_LOAD() {
  pGTASA = aml->GetLib("libGTASA.so");
  if (!pGTASA)
    return;

  // Resolve CTxdStore APIs
  FindTxdSlot =
      reinterpret_cast<FindTxdSlot_t>(pGTASA + OFFSET_CTXDSTORE_FIND_SLOT);
  PushCurrentTxd =
      reinterpret_cast<PushCurrentTxd_t>(pGTASA + OFFSET_CTXDSTORE_PUSH_TXD);
  SetCurrentTxd =
      reinterpret_cast<SetCurrentTxd_t>(pGTASA + OFFSET_CTXDSTORE_SET_TXD);
  PopCurrentTxd =
      reinterpret_cast<PopCurrentTxd_t>(pGTASA + OFFSET_CTXDSTORE_POP_TXD);

  // Resolve RenderWare APIs
  RwTextureRead =
      reinterpret_cast<RwTextureRead_t>(pGTASA + OFFSET_RW_TEXTURE_READ);
  g_pEnvMapPluginOffset =
      reinterpret_cast<int32_t *>(pGTASA + OFFSET_ENV_MAP_PLUGIN_OFFSET);

  HOOK(CustomPipeMaterialSetup, pGTASA + OFFSET_CUSTOM_PIPE_MAT_SETUP);

  // Load texture during initialization
  LoadGlareTexture();
}
