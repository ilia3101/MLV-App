/* galosh_vk_host.c — GALOSH o32 Vulkan host, library mode for MLV-App.
 *
 * Derived from the upstream standalone Vulkan host.  The dispatch graph and
 * shader set are unchanged; the CLI/file I/O wrapper was replaced by
 * galosh_vk_run(), which takes float in/out buffers.  Device and pipelines
 * are created once per process; transient buffers are released after each
 * frame.
 *
 * Env: GALOSH_VERBOSE, GALOSH_DUMP_DIR, GALOSH_O32_PHASE_STRIDE,
 *      GALOSH_BAND_RATE_MS (override the learned pass12 ms/WG-row rate)
 * Shaders: supplied via galosh_vulkan_set_shader_dir().
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <vulkan/vulkan.h>

#include "galosh_vulkan.h"

/* Portability macros are present in current Vulkan headers, but keep local
 * fallbacks so this compiles against older SDKs too. */
#ifndef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
#define VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME "VK_KHR_portability_enumeration"
#endif
#ifndef VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
#define VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME "VK_KHR_get_physical_device_properties2"
#endif
#ifndef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
#define VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME "VK_KHR_portability_subset"
#endif
#ifndef VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
#define VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR 0x00000001
#endif

#define CHECK(x) do { VkResult _r = (x); if(_r != VK_SUCCESS) { \
  fprintf(stderr, "VK error %d at %s:%d\n", _r, __FILE__, __LINE__); exit(1); } } while(0)

/* ---- blueprint constants ---- */
#define PARAMS_SIZE     32
#define GAT_LUT_SIZE    4096
#define N_REDUCE_WG     64
#define O32_TILE        28
#define P_S_SCALE       10
#define P_LUMA_STR      11
#define P_CHROMA_STR    12
#define P_ALPHA         13
#define P_SIGMA_SQ      14
#define P_DARK_THRESH   15

static int g_verbose = 0;

/* ================= context ================= */
static VkInstance       g_inst;
static VkPhysicalDevice g_pd;
static VkDevice         g_dev;
static VkQueue          g_q;
static uint32_t         g_qi;
static VkCommandPool    g_pool;
static VkPhysicalDeviceProperties g_props;
static VkDescriptorPool g_dpool;
static VkQueryPool      g_qpool;
static uint32_t         g_ts_n = 0;
static const char      *g_ts_label[256];
static float            g_band_rate_ms = 0.0f;  /* learned pass12 ms/WG-row */

typedef struct { VkBuffer buf; VkDeviceMemory mem; VkDeviceSize size; } Buf;

/* Library mode: shaders live next to the application or in a Qt resource
 * directory supplied by the caller. */
static char g_shader_dir[1024];

/* Every buffer created by mkbuf is registered here so a library call can
 * release its transient allocations without knowing every local name. */
#define GALOSH_MAX_BUFS 256
static Buf g_live_bufs[GALOSH_MAX_BUFS];
static int g_live_buf_count = 0;

static void galosh_destroy_live_bufs(void)
{
  if(!g_dev) return;
  for(int i = 0; i < g_live_buf_count; i++)
  {
    if(g_live_bufs[i].buf) vkDestroyBuffer(g_dev, g_live_bufs[i].buf, NULL);
    if(g_live_bufs[i].mem) vkFreeMemory(g_dev, g_live_bufs[i].mem, NULL);
  }
  g_live_buf_count = 0;
}

/* Create the Vulkan instance with the portability extensions required by
 * MoltenVK / the macOS Vulkan loader.  The extensions are only added when the
 * loader advertises them, so the same path works on Windows/Linux. */
static VkResult galosh_vk_create_instance(VkInstance *out)
{
  uint32_t next = 0;
  VkResult r = vkEnumerateInstanceExtensionProperties(NULL, &next, NULL);
  if(r != VK_SUCCESS) return r;
  VkExtensionProperties *ext = next ? malloc(next * sizeof(*ext)) : NULL;
  if(next && !ext) return VK_ERROR_OUT_OF_HOST_MEMORY;
  if(next)
  {
    r = vkEnumerateInstanceExtensionProperties(NULL, &next, ext);
    if(r != VK_SUCCESS) { free(ext); return r; }
  }

  const char *enabled[4];
  uint32_t enabled_count = 0;
  int has_portability = 0;
  for(uint32_t i = 0; i < next; i++)
  {
    if(strcmp(ext[i].extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
      has_portability = 1;
    if(strcmp(ext[i].extensionName, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) == 0)
      enabled[enabled_count++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
  }
  if(has_portability)
    enabled[enabled_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
  free(ext);

  VkApplicationInfo ai = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .pApplicationName = "mlvapp-galosh",
                           .apiVersion = VK_API_VERSION_1_2 };
  VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                              .pApplicationInfo = &ai,
                              .enabledExtensionCount = enabled_count,
                              .ppEnabledExtensionNames = enabled_count ? enabled : NULL };
  if(has_portability)
    ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

  return vkCreateInstance(&ci, NULL, out);
}

/* The FP16 storage contract is required by the current shader set.  Query it
 * before exposing the GPU backend so unsupported devices select CPU instead
 * of failing during device creation. */
static int galosh_vk_supports_f16(VkPhysicalDevice pd)
{
  VkPhysicalDevice16BitStorageFeatures storage = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES };
  VkPhysicalDeviceShaderFloat16Int8Features half = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
    .pNext = &storage };
  VkPhysicalDeviceFeatures2 f2 = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
    .pNext = &half };
  vkGetPhysicalDeviceFeatures2(pd, &f2);
  return storage.storageBuffer16BitAccess && half.shaderFloat16;
}

#ifdef __APPLE__
/* A Finder-launched app does not inherit the shell's Vulkan environment.
 * Probe the common bundled/Homebrew ICD locations before instance creation. */
static void galosh_vk_prepare_macos_icd(void)
{
  if(getenv("VK_ICD_FILENAMES") || getenv("VK_DRIVER_FILES")) return;

  char local[1200];
  const char *candidates[4];
  int n = 0;
  candidates[n++] = "MoltenVK_icd.json";
  if(g_shader_dir[0])
  {
    snprintf(local, sizeof(local), "%s/MoltenVK_icd.json", g_shader_dir);
    candidates[n++] = local;
  }
  candidates[n++] = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json";
  candidates[n++] = "/usr/local/etc/vulkan/icd.d/MoltenVK_icd.json";

  for(int i = 0; i < n; i++)
  {
    if(access(candidates[i], R_OK) == 0)
    {
      setenv("VK_ICD_FILENAMES", candidates[i], 1);
      setenv("VK_DRIVER_FILES", candidates[i], 1);
      break;
    }
  }
}
#endif

static uint32_t find_mem(uint32_t bits, VkMemoryPropertyFlags want)
{
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(g_pd, &mp);
  for(uint32_t i = 0; i < mp.memoryTypeCount; i++)
    if((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
      return i;
  fprintf(stderr, "no memtype 0x%x\n", want); exit(1);
}

static Buf mkbuf(VkDeviceSize sz, VkBufferUsageFlags use, VkMemoryPropertyFlags props)
{
  Buf b; b.size = sz;
  VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                            .size = sz, .usage = use,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
  CHECK(vkCreateBuffer(g_dev, &bi, NULL, &b.buf));
  VkMemoryRequirements mr; vkGetBufferMemoryRequirements(g_dev, b.buf, &mr);
  VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                               .allocationSize = mr.size,
                               .memoryTypeIndex = find_mem(mr.memoryTypeBits, props) };
  CHECK(vkAllocateMemory(g_dev, &mai, NULL, &b.mem));
  CHECK(vkBindBufferMemory(g_dev, b.buf, b.mem, 0));
  if(g_live_buf_count < GALOSH_MAX_BUFS)
    g_live_bufs[g_live_buf_count++] = b;
  return b;
}
#define DEVBUF(sz) mkbuf((sz), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | \
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, \
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)

/* ================= pipelines ================= */
typedef struct {
  const char     *spv;      /* shaders/<spv>.spv */
  int             nbind;    /* SSBO bindings */
  int             push;     /* push-constant bytes */
  int             sg32;     /* [B7g] pin subgroup size 32 (needs g_sg_ok) */
  VkDescriptorSetLayout dsl;
  VkPipelineLayout      pl;
  VkPipeline            pipe;
} Kern;

/* [B7g] VK_EXT_subgroup_size_control usable on this device (probed). */
static int g_sg_ok = 0;

/* Active kernels (dead ones excluded per blueprint). Order = KID enum. */
enum {
  K_NE_STATS, K_NE_FIN, K_NE_DT_HIST, K_NE_DT_FIN, K_NE_DL_HIST, K_NE_D_FIN,
  K_LUT_BUILD, K_LUT_FIN, K_GAT_FWD, K_SIGMA_CFA, K_UNIFIED, K_NORMALIZE,
  K_DR_REDUCE, K_DR_FIN, K_DR_RREDUCE, K_DR_RFIN, K_DARK_SUB,
  K_FWD_L, K_CHROMA_EX, K_PASS12, K_PASS1_DUMP, K_P6_FUSED,
  K_BOX2, K_BOX2_3P, K_LOESS_T, K_CROP, K_K16, K_PAD, K_SMOOTH, K_INV,
  K_PASS12_W4, K_FASTUP,          /* [B6] fast-mode siblings */
  /* [FP16 v1] site variants (dataflow_spec.md §4): contract-buffer f16
   * bindings at specific dispatch sites; the f32 originals keep serving
   * the pyramid-internal sites.  ONE shader set, site-selected — never
   * per-GPU. */
  K_BOX2_H16, K_CROP_H16, K_LOESS_T_G16, K_K16_F16, K_FASTUP_F16,
  K_SIGMA_HIST, K_SIGMA_FIN,   /* [iGPU-opt] two-stage sigma_per_cfa */
  /* [B7f fusion pack] 3-plane pad (9 dispatches -> 3) + final upsample x
   * inverse fusion (Cal1..3 full-res planes never materialized on the
   * common path; ~100 MB traffic saved at 4K).  K_PAD / K_K16_F16 /
   * K_FASTUP_F16 / K_INV stay compiled for the GALOSH_DUMP_DIR path. */
  K_PAD3, K_K16_INV_F, K_FASTUP_INV_F,
  /* [B7g] subgroup-cooperative pass12 (quality 8x8; spill-free rewrite).
   * Created ONLY when g_sg_ok; classic o32_pass12 stays the fallback and
   * the GALOSH_SG=0 A/B reference. */
  K_PASS12_SG,
  K_COUNT
};
static Kern g_k[K_COUNT] = {
  [K_NE_STATS]  = { "o32_ne_block_stats",         3, 20 },
  [K_NE_FIN]    = { "o32_ne_finalize",            4, 12 },
  [K_NE_DT_HIST]= { "o32_ne_dark_thresh_hist",    2,  8 },
  [K_NE_DT_FIN] = { "o32_ne_dark_thresh_finalize",2,  4 },
  [K_NE_DL_HIST]= { "o32_ne_dark_lap_hist",       3, 12 },
  [K_NE_D_FIN]  = { "o32_ne_dark_finalize",       2,  4 },
  [K_LUT_BUILD] = { "o32_build_inv_lut",          4,  0 },
  [K_LUT_FIN]   = { "o32_lut_finalize",           2,  0 },
  [K_GAT_FWD]   = { "o32_gat_forward_full",       7,  8 },
  [K_SIGMA_CFA] = { "o32_sigma_per_cfa",          2,  8 },
  [K_UNIFIED]   = { "o32_unified_sigma",          1,  0 },
  [K_NORMALIZE] = { "o32_normalize_apply",        6,  8 },
  [K_DR_REDUCE] = { "o32_dark_ref_reduce_mwg",    4,  8 },
  [K_DR_FIN]    = { "o32_dark_ref_finalize_mwg",  2,  4 },
  [K_DR_RREDUCE]= { "o32_dark_resid_reduce_mwg",  4,  8 },
  [K_DR_RFIN]   = { "o32_dark_resid_finalize_mwg",2, 12 },
  [K_DARK_SUB]  = { "o32_dark_sub_full",          6,  8 },
  [K_FWD_L]     = { "o32_forward_l_stride1",      2,  8 },
  [K_CHROMA_EX] = { "o32_chroma_extract_halfres", 4, 16 },
  [K_PASS12]    = { "o32_pass12",                 2, 16 },
  [K_PASS1_DUMP]= { "o32_pass1_dump",             2, 12 },
  [K_P6_FUSED]  = { "o32_lpixel_lh_den_fused",    3, 12 },
  [K_BOX2]      = { "o32_box_downsample_2x",      2,  8 },
  [K_BOX2_3P]   = { "o32_box_downsample_2x_3p",   6,  8 },
  [K_LOESS_T]   = { "o32_loess_chroma_3p_tiled",  7, 12 },
  [K_CROP]      = { "o32_crop_2d_topleft",        2, 16 },
  [K_K16]       = { "o32_k16_jbu_3p",             7, 12 },
  [K_PAD]       = { "o32_pad_2d_edge",            2, 16 },
  [K_SMOOTH]    = { "o32_smoothstep_blend_3p",   15, 12 },
  [K_INV]       = { "o32_inverse_wht_dark_gat",   9,  8 },
  [K_PASS12_W4] = { "o32_pass12_wht4",            2, 16 },
  [K_FASTUP]    = { "o32_fastup_3p",              7, 12 },
  /* [FP16 v1] same nbind/push as their f32 originals */
  [K_BOX2_H16]   = { "o32_box_downsample_2x_h16",     2,  8 },
  [K_CROP_H16]   = { "o32_crop_2d_topleft_h16",       2, 16 },
  [K_LOESS_T_G16]= { "o32_loess_chroma_3p_tiled_g16", 7, 12 },
  [K_K16_F16]    = { "o32_k16_jbu_3p_f16",            7, 12 },
  [K_FASTUP_F16] = { "o32_fastup_3p_f16",             7, 12 },
  [K_SIGMA_HIST] = { "o32_sigma_hist_mwg",            2, 12 },
  [K_SIGMA_FIN]  = { "o32_sigma_fin_mwg",             2,  0 },
  /* [B7f fusion pack] */
  [K_PAD3]        = { "o32_pad_2d_edge_3p",       6, 16 },
  [K_K16_INV_F]   = { "o32_k16_inverse_fused",    9, 12 },
  [K_FASTUP_INV_F]= { "o32_fastup_inverse_fused", 9, 12 },
  /* [B7g] same bindings/push as o32_pass12; local size 512, sg32 pinned */
  [K_PASS12_SG]   = { "o32_pass12_sg",            2, 16, .sg32 = 1 },
};

static VkShaderModule load_spv(const char *name)
{
  char path[1200];
  snprintf(path, sizeof(path), "%s/%s.spv", g_shader_dir[0] ? g_shader_dir : "shaders", name);
  FILE *f = fopen(path, "rb");
  if(!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
  fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
  uint32_t *code = malloc(sz);
  if(fread(code, 1, sz, f) != (size_t)sz) { fprintf(stderr, "short read %s\n", path); exit(1); }
  fclose(f);
  VkShaderModuleCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                  .codeSize = (size_t)sz, .pCode = code };
  VkShaderModule m; CHECK(vkCreateShaderModule(g_dev, &ci, NULL, &m));
  free(code); return m;
}

static void make_kernel(Kern *k)
{
  VkDescriptorSetLayoutBinding binds[16];
  for(int i = 0; i < k->nbind; i++)
    binds[i] = (VkDescriptorSetLayoutBinding){ .binding = (uint32_t)i,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
  VkDescriptorSetLayoutCreateInfo dsli = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = (uint32_t)k->nbind, .pBindings = binds };
  CHECK(vkCreateDescriptorSetLayout(g_dev, &dsli, NULL, &k->dsl));
  VkPushConstantRange pcr = { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                              .offset = 0, .size = (uint32_t)(k->push ? k->push : 4) };
  VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 1, .pSetLayouts = &k->dsl,
    .pushConstantRangeCount = k->push ? 1u : 0u, .pPushConstantRanges = &pcr };
  CHECK(vkCreatePipelineLayout(g_dev, &pli, NULL, &k->pl));
  VkShaderModule sm = load_spv(k->spv);
  /* DISPATCH_BASE on every compute pipeline: enables the TDR-safe banded
   * pass12 dispatch via vkCmdDispatchBase (harmless for plain dispatch). */
  VkComputePipelineCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .flags = VK_PIPELINE_CREATE_DISPATCH_BASE_BIT,
    .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
               .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = sm, .pName = "main" },
    .layout = k->pl };
  /* [B7g] pin the subgroup size to 32 + require full subgroups: the
   * lane<->coefficient mapping of o32_pass12_sg is exact only at S=32.
   * One code path — every probed GPU (NVIDIA native-32, AMD wave32,
   * Arc 8/16/32) accepts requiredSubgroupSize=32.  Caller guarantees
   * g_sg_ok when k->sg32 is set. */
  VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT rss = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT,
    .requiredSubgroupSize = 32 };
  if(k->sg32)
  {
    cpi.stage.pNext = &rss;
    cpi.stage.flags = VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT;
  }
  CHECK(vkCreateComputePipelines(g_dev, VK_NULL_HANDLE, 1, &cpi, NULL, &k->pipe));
  vkDestroyShaderModule(g_dev, sm, NULL);
}

static VkDescriptorSet mkset(int kid, const Buf *bufs[], int n)
{
  if(n != g_k[kid].nbind)
  { fprintf(stderr, "mkset %s: %d bufs != %d binds\n", g_k[kid].spv, n, g_k[kid].nbind); exit(1); }
  VkDescriptorSetAllocateInfo a = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool = g_dpool, .descriptorSetCount = 1, .pSetLayouts = &g_k[kid].dsl };
  VkDescriptorSet ds; CHECK(vkAllocateDescriptorSets(g_dev, &a, &ds));
  VkDescriptorBufferInfo bi[16];
  VkWriteDescriptorSet wr[16];
  for(int i = 0; i < n; i++)
  {
    bi[i] = (VkDescriptorBufferInfo){ .buffer = bufs[i]->buf, .range = VK_WHOLE_SIZE };
    wr[i] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = ds, .dstBinding = (uint32_t)i, .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi[i] };
  }
  vkUpdateDescriptorSets(g_dev, (uint32_t)n, wr, 0, NULL);
  return ds;
}

/* ================= command recording ================= */
static VkCommandBuffer cb_begin(void)
{
  VkCommandBufferAllocateInfo a = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = g_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
  VkCommandBuffer cb; CHECK(vkAllocateCommandBuffers(g_dev, &a, &cb));
  VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
  CHECK(vkBeginCommandBuffer(cb, &bi));
  return cb;
}

static void cb_submit_wait(VkCommandBuffer cb)
{
  CHECK(vkEndCommandBuffer(cb));
  VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                      .commandBufferCount = 1, .pCommandBuffers = &cb };
  CHECK(vkQueueSubmit(g_q, 1, &si, VK_NULL_HANDLE));
  CHECK(vkQueueWaitIdle(g_q));
  vkFreeCommandBuffers(g_dev, g_pool, 1, &cb);
}

static void barrier(VkCommandBuffer cb)
{
  VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                     VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT };
  vkCmdPipelineBarrier(cb,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
    0, 1, &mb, 0, NULL, 0, NULL);
}

/* push-constant scratch: mixed int/float fields, 4 bytes each */
typedef union { int32_t i; float f; } PcW;

static void dispatch_k(VkCommandBuffer cb, int kid, VkDescriptorSet ds,
                       const PcW *pc, int npc,
                       uint32_t gx, uint32_t gy, uint32_t gz, const char *label)
{
  Kern *k = &g_k[kid];
  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, k->pipe);
  vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, k->pl, 0, 1, &ds, 0, NULL);
  if(npc) vkCmdPushConstants(cb, k->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                             (uint32_t)(npc * 4), pc);
  if(g_verbose && g_ts_n < 255)
  {
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_qpool, g_ts_n);
    g_ts_label[g_ts_n] = label;
  }
  vkCmdDispatch(cb, gx, gy, gz);
  if(g_verbose && g_ts_n < 255)
  {
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_qpool, g_ts_n + 1);
    g_ts_n += 2;
  }
  barrier(cb);
}

#define AUP(n, a) ((((n) + (a) - 1) / (a)))   /* group count for local size a */

/* [TDR-safe] Banded dispatch via vkCmdDispatchBase (core 1.1): splits the
 * workgroup grid into NB row bands = NB independent dispatches = preemption
 * boundaries, so one long kernel (pass12 at 4K on a slow iGPU measured
 * ~2.9 s single-dispatch = over the Windows ~2 s TDR watchdog) can never
 * starve the OS.  Tiles are independent — zero numeric change, no barrier
 * between bands.  Single code path on every GPU (8 x 5 ms on a 4070 Ti is
 * harmless).  JP: 行バンド分割で TDR 恒久回避。数値は完全不変。 */
static VkCommandBuffer cb_begin(void);
static void cb_submit_wait(VkCommandBuffer cb);

/* Each band = its OWN queue submission: measured on the AMD iGPU, the
 * Windows TDR watchdog treats one submission as a single non-preemptible
 * unit (banding via vkCmdDispatchBase inside ONE command buffer still
 * died at 4K), so preemption boundaries must be SUBMISSION boundaries.
 * The 8 submissions are queued back-to-back with a single vkQueueWaitIdle
 * at the end — zero host round-trips between bands, so the overhead on a
 * fast dGPU is ~0 while a slow iGPU gets 8 watchdog-visible chunks.
 * Timestamps: one pair spanning first..last band.
 * JP: バンド=サブミッション境界（TDR の preemption 単位）。待ちは最後に
 * 1 回だけなので高速 GPU への追加コストは実質ゼロ。 */
/* Queue one band piece as its OWN submission (= TDR preemption unit)
 * WITHOUT waiting — caller decides when to vkQueueWaitIdle.  Returns the
 * command buffer for later freeing. */
static VkCommandBuffer band_piece(int kid, VkDescriptorSet ds, const PcW *pc, int npc,
                       uint32_t gx, uint32_t y0, uint32_t rows,
                       int ts_open, int ts_close, const char *label)
{
  Kern *k = &g_k[kid];
  VkCommandBuffer cb = cb_begin();
  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, k->pipe);
  vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, k->pl, 0, 1, &ds, 0, NULL);
  if(npc) vkCmdPushConstants(cb, k->pl, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                             (uint32_t)(npc * 4), pc);
  if(g_verbose && ts_open && g_ts_n < 255)
  {
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_qpool, g_ts_n);
    g_ts_label[g_ts_n] = label;
  }
  vkCmdDispatchBase(cb, 0, y0, 0, gx, rows, 1);
  if(g_verbose && ts_close && g_ts_n < 255)
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_qpool, g_ts_n + 1);
  barrier(cb);
  CHECK(vkEndCommandBuffer(cb));
  VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                      .commandBufferCount = 1, .pCommandBuffers = &cb };
  CHECK(vkQueueSubmit(g_q, 1, &si, VK_NULL_HANDLE));
  return cb;
}

/* Watchdog-aware ADAPTIVE banding: submit the first 1/8 of the workgroup
 * rows alone and wall-clock it.  Fast GPUs (< 150 ms for the probe =>
 * whole kernel well under the ~2 s Windows TDR budget) get the remaining
 * 7/8 as ONE dispatch (overhead: a single extra submit).  Slow GPUs get
 * the remainder split so each piece stays ~<= 1 s.  Adaptation is by
 * MEASURED BEHAVIOR, not vendor detection — one code path everywhere.
 * (Empirical basis: an AMD iGPU ran pass12@4K ~7.7 s; a single in-CB
 * vkCmdDispatchBase banding still died => the TDR preemption unit is the
 * SUBMISSION, hence per-piece submissions here.)
 * JP: 先頭バンドを実測して分割数を自己決定。ベンダー判定は使わない。 */
static void dispatch_k_banded(int kid, VkDescriptorSet ds,
                              const PcW *pc, int npc,
                              uint32_t gx, uint32_t gy, const char *label)
{
  /* Probe = FIXED 2 workgroup rows (not a fraction of the image): bounded
   * on any GPU speed x any resolution (an iGPU-8K-slow probe is still a
   * few hundred ms, never near the ~2 s watchdog).  A fractional probe
   * (1/8) could itself TDR in the extreme corner (measured iGPU pass12
   * ~7.7 s at 4K => ~31 s at 8K => 1/8 probe ~3.9 s = dead).
   * JP: プローブは固定 2 タイル行。極端ケースでもプローブ自体が
   * 監視時間を踏み抜かないことを構成的に保証。 */
  const uint32_t probe_rows = (gy > 2) ? 2u : gy;
  VkCommandBuffer cbs[64];
  uint32_t ncb = 0;
  const clock_t t_all = clock();
  double per_row_ms;

  if(g_band_rate_ms > 0.0f)
  {
    /* [iGPU-opt] rate learned from a previous frame (video state): no
     * probe round-trip at all — every piece queued back-to-back, ONE
     * wait at the end.  レート既知ならプローブ往復ゼロ。 */
    per_row_ms = (double)g_band_rate_ms;
    const double est_ms = per_row_ms * (double)gy;
    uint32_t pieces = (est_ms > 800.0) ? (uint32_t)(est_ms / 800.0) + 1 : 1;
    /* Never rely on a single submission for pass12.  The probe can
     * underestimate the per-row rate (driver sync overhead / warm-up),
     * and one oversized submission trips the GPU watchdog.  Eight pieces
     * cost microseconds on a fast dGPU and keep slow iGPUs safe. */
    if(pieces < 8 && gy >= 8) pieces = 8;
    if(pieces > gy) pieces = gy;
    if(pieces > 64) pieces = 64;
    for(uint32_t p = 0; p < pieces; p++)
    {
      const uint32_t a = gy * p / pieces, b = gy * (p + 1) / pieces;
      if(b > a) cbs[ncb++] = band_piece(kid, ds, pc, npc, gx, a, b - a,
                                        p == 0, p == pieces - 1, label);
    }
    CHECK(vkQueueWaitIdle(g_q));
  }
  else
  {
    /* unknown rate: bounded 2-row probe (sync), then queue the rest */
    cbs[ncb++] = band_piece(kid, ds, pc, npc, gx, 0, probe_rows, 1,
                            probe_rows >= gy, label);
    CHECK(vkQueueWaitIdle(g_q));
    const double probe_ms = 1000.0 * (double)(clock() - t_all) / CLOCKS_PER_SEC;
    if(probe_rows < gy)
    {
      const uint32_t rest_y0 = probe_rows, rest = gy - probe_rows;
      per_row_ms = probe_ms / (double)probe_rows;
      const double est_rest_ms = per_row_ms * (double)rest;
      uint32_t pieces = (est_rest_ms > 800.0) ? (uint32_t)(est_rest_ms / 800.0) + 1 : 1;
      if(pieces < 8 && rest >= 8) pieces = 8;
      if(pieces > rest) pieces = rest;
      if(pieces > 63) pieces = 63;
      for(uint32_t p = 0; p < pieces; p++)
      {
        const uint32_t a = rest_y0 + rest * p / pieces;
        const uint32_t b = rest_y0 + rest * (p + 1) / pieces;
        if(b > a) cbs[ncb++] = band_piece(kid, ds, pc, npc, gx, a, b - a,
                                          0, p == pieces - 1, label);
      }
      CHECK(vkQueueWaitIdle(g_q));
    }
  }
  vkFreeCommandBuffers(g_dev, g_pool, ncb, cbs);
  /* learn/refresh the rate for the next frame (written to the state file) */
  const double total_ms = 1000.0 * (double)(clock() - t_all) / CLOCKS_PER_SEC;
  g_band_rate_ms = (float)(total_ms / (double)gy);
  if(g_verbose && g_ts_n < 255) g_ts_n += 2;
}

/* ================= staging I/O ================= */
static Buf g_stg; static void *g_stg_map;

static void upload(Buf *dst, const void *src, VkDeviceSize sz, VkDeviceSize dst_off)
{
  memcpy(g_stg_map, src, sz);
  VkCommandBuffer cb = cb_begin();
  VkBufferCopy c = { .srcOffset = 0, .dstOffset = dst_off, .size = sz };
  vkCmdCopyBuffer(cb, g_stg.buf, dst->buf, 1, &c);
  cb_submit_wait(cb);
}

static void download(Buf *src, void *dst, VkDeviceSize sz, VkDeviceSize src_off)
{
  VkCommandBuffer cb = cb_begin();
  VkBufferCopy c = { .srcOffset = src_off, .dstOffset = 0, .size = sz };
  vkCmdCopyBuffer(cb, src->buf, g_stg.buf, 1, &c);
  cb_submit_wait(cb);
  memcpy(dst, g_stg_map, sz);
}

static void dump_buf(const char *dir, const char *name, Buf *b, size_t nfloats)
{
  if(!dir) return;
  float *tmp = malloc(nfloats * 4);
  download(b, tmp, nfloats * 4, 0);
  char path[1200]; snprintf(path, sizeof(path), "%s/%s.bin", dir, name);
  FILE *f = fopen(path, "wb");
  if(f) { fwrite(tmp, 4, nfloats, f); fclose(f); }
  free(tmp);
}

/* [FP16 v1] IEEE binary16 → binary32 widen (exact; handles subnormal /
 * inf / NaN).  Host-side only — used to keep the GALOSH_DUMP_DIR .bin
 * dumps and the small-image guard output in f32 format while the device
 * buffers hold f16.  (日) ダンプ形式は f32 のまま、ホストで拡張。 */
static float half_to_float(uint16_t h)
{
  const uint32_t s = (uint32_t)(h & 0x8000u) << 16;
  uint32_t e = (h >> 10) & 0x1Fu;
  uint32_t m = h & 0x3FFu;
  uint32_t bits;
  if(e == 0)
  {
    if(m == 0) bits = s;                        /* ±0 */
    else
    {                                           /* subnormal: renormalize */
      e = 127 - 15 + 1;
      while(!(m & 0x400u)) { m <<= 1; e--; }
      m &= 0x3FFu;
      bits = s | (e << 23) | (m << 13);
    }
  }
  else if(e == 31) bits = s | 0x7F800000u | (m << 13);      /* inf / NaN */
  else             bits = s | ((e - 15 + 127) << 23) | (m << 13);
  float f; memcpy(&f, &bits, 4);
  return f;
}

/* [FP16 v1] dump a contract (f16-storage) buffer: read raw binary16,
 * widen on the host, write the usual f32 .bin (verify-harness compat). */
static void dump_buf_f16(const char *dir, const char *name, Buf *b, size_t nvals)
{
  if(!dir) return;
  uint16_t *raw16 = malloc(nvals * 2);
  download(b, raw16, nvals * 2, 0);
  float *tmp = malloc(nvals * 4);
  for(size_t i = 0; i < nvals; i++) tmp[i] = half_to_float(raw16[i]);
  char path[1200]; snprintf(path, sizeof(path), "%s/%s.bin", dir, name);
  FILE *f = fopen(path, "wb");
  if(f) { fwrite(tmp, 4, nvals, f); fclose(f); }
  free(tmp); free(raw16);
}

/* ================= library entry ================= */
static int galosh_vk_run(const float *input, float *output,
                         int W, int H,
                         float strength, float luma_str, float chroma_str,
                         float alpha_ext, float sig_ext, int ext_model,
                         int wht_block, int upsample_fast)
{
  if(!input || !output || W <= 0 || H <= 0 || (W & 1) || (H & 1)) return 1;
  g_verbose = getenv("GALOSH_VERBOSE") != NULL;
  g_ts_n = 0;
  { const char *e = getenv("GALOSH_BAND_RATE_MS");
    if(e) g_band_rate_ms = (float)atof(e); }
  const char *dump_dir = getenv("GALOSH_DUMP_DIR");
  const char *noise_mode = "fit";
  const char *noise_state_path = NULL;

  int phase_stride = 1;
  { const char *e = getenv("GALOSH_O32_PHASE_STRIDE");
    if(e) { phase_stride = atoi(e); if(phase_stride < 1) phase_stride = 1; } }

  /* --- geometry (blueprint §1) --- */
  const int hw = W / 2, hh = H / 2;
  const size_t npix = (size_t)W * H, chsize = (size_t)hw * hh;
  const int cq_w = hw / 2, cq_h = hh / 2;
  const int ce_w = cq_w / 2, ce_h = cq_h / 2;
  const int kq_w = 2 * cq_w, kq_h = 2 * cq_h;
  const int ke_w = 2 * ce_w, ke_h = 2 * ce_h;
  const size_t full_f = npix * 4, ch_f = chsize * 4;
  /* [FP16 v1] contract buffers stored as binary16 → halved byte sizes
   * (dataflow_spec.md §4). */
  const size_t full_h16 = npix * 2, ch_h16 = chsize * 2;
  const size_t cq_f = (size_t)cq_w * cq_h * 4, ce_f = (size_t)ce_w * ce_h * 4;
  const size_t kq_f = (size_t)kq_w * kq_h * 4, ke_f = (size_t)ke_w * ke_h * 4;
  const int ne_n_bx = hw / 8, ne_n_by = hh / 8;
  const int ne_per_ch = ne_n_bx * ne_n_by;
  const size_t ne_total = (size_t)4 * ne_per_ch;
  /* P0e geometry (blueprint dispatch #3) */
  const int hw_a = (W + 1) / 2, hh_a = (H + 1) / 2;
  const int hw_3 = (hw_a + 2) / 3, hh_3 = (hh_a + 2) / 3;

  /* --- instance / device (created once per process) --- */
  if(!g_dev)
  {
#ifdef __APPLE__
  galosh_vk_prepare_macos_icd();
#endif
  CHECK(galosh_vk_create_instance(&g_inst));
  uint32_t nd = 0; vkEnumeratePhysicalDevices(g_inst, &nd, NULL);
  VkPhysicalDevice pds[8]; if(nd > 8) nd = 8;
  vkEnumeratePhysicalDevices(g_inst, &nd, pds);
  /* vkEnumeratePhysicalDevices ORDER IS NOT STABLE (observed: the list
   * reordered after a TDR device reset).  Build a DETERMINISTIC order:
   * discrete GPUs first, then integrated, then the rest; ties by name.
   * GALOSH_VK_DEVICE accepts an index into THIS sorted order, or a
   * case-insensitive name substring ("NVIDIA" / "Radeon" / "Arc").
   * JP: 列挙順は無保証（TDR リセットで実際に入れ替わった）→ 決定的
   * ソート＋名前マッチ選択。 */
  VkPhysicalDeviceProperties pp[8];
  uint32_t order[8];
  for(uint32_t i = 0; i < nd; i++)
  { vkGetPhysicalDeviceProperties(pds[i], &pp[i]); order[i] = i; }
  for(uint32_t a = 0; a + 1 < nd; a++)
    for(uint32_t b = a + 1; b < nd; b++)
    {
      #define TYPE_RANK(t) ((t) == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 0 : \
                            (t) == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1 : 2)
      const int ra = TYPE_RANK(pp[order[a]].deviceType);
      const int rb = TYPE_RANK(pp[order[b]].deviceType);
      if(rb < ra || (rb == ra &&
         strcmp(pp[order[b]].deviceName, pp[order[a]].deviceName) < 0))
      { uint32_t t = order[a]; order[a] = order[b]; order[b] = t; }
    }
  uint32_t di = order[0];
  { const char *e = getenv("GALOSH_VK_DEVICE");
    if(e && e[0])
    {
      if(e[0] >= '0' && e[0] <= '9')
      { uint32_t w = (uint32_t)atoi(e); if(w < nd) di = order[w]; }
      else
      {
        for(uint32_t i = 0; i < nd; i++)
        {
          /* case-insensitive substring match on the device name */
          const char *hay = pp[i].deviceName;
          for(const char *h = hay; *h; h++)
          {
            const char *n = e, *q = h;
            while(*n && *q &&
                  ((*n | 32) == (*q | 32))) { n++; q++; }
            if(!*n) { di = i; goto dev_found; }
          }
        }
        dev_found: ;
      }
    }
  }
  g_pd = pds[di];
  vkGetPhysicalDeviceProperties(g_pd, &g_props);
  fprintf(stderr, "[vk] device[%u] = %s\n", di, g_props.deviceName);

  if(!galosh_vk_supports_f16(g_pd))
  {
    fprintf(stderr, "[vk] device does not support the FP16 storage contract\n");
    vkDestroyInstance(g_inst, NULL);
    g_inst = VK_NULL_HANDLE;
    g_pd = VK_NULL_HANDLE;
    return 1;
  }

  /* [B7g] probe VK_EXT_subgroup_size_control: the subgroup-cooperative
   * pass12 needs subgroup size PINNED to 32 (its lane<->coefficient map).
   * Capability-gated, not vendor-gated — all three probe GPUs support it
   * (NVIDIA native-32, AMD wave32-capable RDNA, Arc min8/max32, and
   * MoltenVK on Apple GPUs). */
  int has_portability_subset = 0;
  {
    uint32_t nx = 0;
    vkEnumerateDeviceExtensionProperties(g_pd, NULL, &nx, NULL);
    VkExtensionProperties *xp = nx ? malloc(nx * sizeof *xp) : NULL;
    if(nx && !xp) { fprintf(stderr, "out of memory\n"); exit(1); }
    if(nx) vkEnumerateDeviceExtensionProperties(g_pd, NULL, &nx, xp);
    int has_sg = 0;
    for(uint32_t i = 0; i < nx; i++)
    {
      if(strcmp(xp[i].extensionName, "VK_EXT_subgroup_size_control") == 0) has_sg = 1;
      if(strcmp(xp[i].extensionName, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME) == 0)
        has_portability_subset = 1;
    }
    free(xp);
    if(has_sg)
    {
      VkPhysicalDeviceSubgroupSizeControlFeaturesEXT sf = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT };
      VkPhysicalDeviceFeatures2 f2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &sf };
      vkGetPhysicalDeviceFeatures2(g_pd, &f2);
      VkPhysicalDeviceSubgroupSizeControlPropertiesEXT sp = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT };
      VkPhysicalDeviceProperties2 p2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &sp };
      vkGetPhysicalDeviceProperties2(g_pd, &p2);
      g_sg_ok = (sf.subgroupSizeControl && sf.computeFullSubgroups &&
                 sp.minSubgroupSize <= 32 && 32 <= sp.maxSubgroupSize &&
                 (sp.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT)) ? 1 : 0;
    }
    /* Escape hatch for drivers that advertise the extension but mis-handle
     * requiredSubgroupSize.  Off by default; capability probe decides. */
    if(getenv("GALOSH_VK_FORCE_CLASSIC")) g_sg_ok = 0;
    if(g_verbose)
      fprintf(stderr, "[vk] subgroup-size-control: %s\n",
              g_sg_ok ? "OK (pass12_sg eligible)" : "absent (classic pass12)");
  }

  /* FP64 stays out of the shader set (Arc lacks shaderFloat64).
   * [FP16 v1] Two features are required for the FP16 storage contract —
   * probe confirmed ALL 3 GPUs (NVIDIA/AMD/Arc) support both, so this
   * remains ONE vendor-agnostic shader set with no per-GPU branching:
   *   - storageBuffer16BitAccess (float16_t SSBO members)
   *   - shaderFloat16            (float16_t casts = the RNE round points) */

  uint32_t qn = 0; vkGetPhysicalDeviceQueueFamilyProperties(g_pd, &qn, NULL);
  VkQueueFamilyProperties qf[16]; if(qn > 16) qn = 16;
  vkGetPhysicalDeviceQueueFamilyProperties(g_pd, &qn, qf);
  g_qi = 0;
  for(uint32_t i = 0; i < qn; i++)
    if(qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { g_qi = i; break; }
  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    .queueFamilyIndex = g_qi, .queueCount = 1, .pQueuePriorities = &prio };
  /* [FP16 v1] enable 16-bit SSBO storage + float16 arithmetic (both core
   * features by Vulkan 1.2; enabled via pNext chain, no extension strings). */
  VkPhysicalDevice16BitStorageFeatures f16stor = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
    .storageBuffer16BitAccess = VK_TRUE };
  VkPhysicalDeviceShaderFloat16Int8Features f16arith = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
    .pNext = &f16stor, .shaderFloat16 = VK_TRUE };
  /* [B7g] chain subgroup-size-control features + enable the extension
   * (only when the probe passed; zero effect otherwise). */
  VkPhysicalDeviceSubgroupSizeControlFeaturesEXT sgcf = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT,
    .subgroupSizeControl = VK_TRUE, .computeFullSubgroups = VK_TRUE };
  if(g_sg_ok) f16stor.pNext = &sgcf;

  const char *dev_exts[4];
  uint32_t dev_ext_count = 0;
  if(has_portability_subset)
    dev_exts[dev_ext_count++] = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
  if(g_sg_ok)
    dev_exts[dev_ext_count++] = "VK_EXT_subgroup_size_control";

  VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext = &f16arith,
    .enabledExtensionCount = dev_ext_count,
    .ppEnabledExtensionNames = dev_ext_count ? dev_exts : NULL,
    .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci, .pEnabledFeatures = NULL };
  CHECK(vkCreateDevice(g_pd, &dci, NULL, &g_dev));
  vkGetDeviceQueue(g_dev, g_qi, 0, &g_q);
  VkCommandPoolCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = g_qi };
  CHECK(vkCreateCommandPool(g_dev, &cpci, NULL, &g_pool));

  /* descriptor pool: ~60 sets, generous SSBO count */
  VkDescriptorPoolSize dps = { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                               .descriptorCount = 512 };
  VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = 96, .poolSizeCount = 1, .pPoolSizes = &dps };
  CHECK(vkCreateDescriptorPool(g_dev, &dpi, NULL, &g_dpool));
  /* Timestamp queries are only needed for the verbose profiler.  Keeping them
   * out of the normal path avoids unnecessary query traffic and the known
   * MoltenVK timestamp-query edge cases. */
  if(g_verbose)
  {
    VkQueryPoolCreateInfo qpci = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 256 };
    CHECK(vkCreateQueryPool(g_dev, &qpci, NULL, &g_qpool));
  }
  else
  {
    g_qpool = VK_NULL_HANDLE;
  }

  for(int i = 0; i < K_COUNT; i++)
  {
    if(g_k[i].sg32 && !g_sg_ok) continue;   /* [B7g] sg kernel needs the ext */
    make_kernel(&g_k[i]);
  }
  } /* !g_dev */

  /* Reset descriptor pool for this frame's descriptor sets. */
  vkResetDescriptorPool(g_dev, g_dpool, 0);

  /* --- buffers (blueprint §1a) --- */
  g_stg = mkbuf(full_f > 1024 ? full_f : 1024,
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  CHECK(vkMapMemory(g_dev, g_stg.mem, 0, VK_WHOLE_SIZE, 0, &g_stg_map));

  Buf raw     = DEVBUF(full_f);
  Buf ch0 = DEVBUF(ch_f), ch1 = DEVBUF(ch_f), ch2 = DEVBUF(ch_f), ch3 = DEVBUF(ch_f);
  Buf params  = DEVBUF(PARAMS_SIZE * 4);
  Buf lut_d   = DEVBUF(GAT_LUT_SIZE * 4), lut_x = DEVBUF(GAT_LUT_SIZE * 4);
  Buf lut_p   = DEVBUF(8 * 4);
  Buf part    = DEVBUF(N_REDUCE_WG * 5 * 2 * sizeof(float));  /* Kahan (sum,comp) f32 pairs */
  Buf part_r  = DEVBUF(N_REDUCE_WG * 2 * 2 * sizeof(float));  /* Kahan (sum,comp) f32 pairs */
  Buf blk_m   = DEVBUF(ne_total * 4), blk_v = DEVBUF(ne_total * 4);
  Buf dt_hist = DEVBUF(4096 * 4), dl_hist = DEVBUF(4096 * 4);
  Buf sigma_hist = DEVBUF(4 * 4096 * 4);   /* [iGPU-opt] two-stage sigma */
  Buf in_gat  = DEVBUF(full_f);
  /* [FP16 v1] contract buffers = f16 storage (half size); C*_h stay f32. */
  Buf L_cs    = DEVBUF(full_h16), L_cs_den = DEVBUF(full_h16);
  Buf L_pixel = DEVBUF(full_h16), L_h_den = DEVBUF(ch_h16);
  Buf C1_h = DEVBUF(ch_f), C2_h = DEVBUF(ch_f), C3_h = DEVBUF(ch_f);
  Buf L_q  = DEVBUF(cq_f > 4 ? cq_f : 4), L_e = DEVBUF(ce_f > 4 ? ce_f : 4);
  Buf L_for_q = DEVBUF(kq_f > 4 ? kq_f : 4), L_for_e = DEVBUF(ke_f > 4 ? ke_f : 4);
#define TRI(name, sz) Buf name##1 = DEVBUF((sz) > 4 ? (sz) : 4), \
                          name##2 = DEVBUF((sz) > 4 ? (sz) : 4), \
                          name##3 = DEVBUF((sz) > 4 ? (sz) : 4)
  TRI(C_q, cq_f); TRI(C_e, ce_f);
  TRI(Cl_h, ch_f); TRI(Cl_q, cq_f); TRI(Cl_e, ce_f);
  TRI(Cqup_r, kq_f); TRI(Cqup, ch_f);
  TRI(Ceq_r, ke_f);  TRI(Ceq, cq_f);
  TRI(Ceup_r, kq_f); TRI(Ceup, ch_f);
  TRI(Cden, ch_h16); TRI(Cal, full_h16);   /* [FP16 v1] contract buffers */
  Buf pilot_dbg = dump_dir ? DEVBUF(full_f)
                           : DEVBUF(4);   /* tiny placeholder when unused */

  /* --- upload input + init params --- */
  float *img = malloc(full_f);
  if(!img) return 1;
  memcpy(img, input, full_f);
  upload(&raw, img, full_f, 0);
  float h_params[PARAMS_SIZE] = { 0 };
  h_params[P_LUMA_STR]   = strength * luma_str;
  h_params[P_CHROMA_STR] = strength * chroma_str;
  upload(&params, h_params, PARAMS_SIZE * 4, 0);

  /* --- descriptor sets per dispatch site --- */
  const Buf *B;
#define SET(kid, ...) mkset(kid, (const Buf *[]){ __VA_ARGS__ }, g_k[kid].nbind)
  (void)B;
  VkDescriptorSet s_ne_stats = SET(K_NE_STATS, &raw, &blk_m, &blk_v);
  VkDescriptorSet s_ne_fin   = SET(K_NE_FIN, &blk_m, &blk_v, &raw, &params);
  VkDescriptorSet s_dt_hist  = SET(K_NE_DT_HIST, &raw, &dt_hist);
  VkDescriptorSet s_dt_fin   = SET(K_NE_DT_FIN, &dt_hist, &params);
  VkDescriptorSet s_dl_hist  = SET(K_NE_DL_HIST, &raw, &params, &dl_hist);
  VkDescriptorSet s_d_fin    = SET(K_NE_D_FIN, &dl_hist, &params);
  VkDescriptorSet s_lut      = SET(K_LUT_BUILD, &params, &lut_d, &lut_x, &lut_p);
  VkDescriptorSet s_lut_fin  = SET(K_LUT_FIN, &lut_d, &lut_p);
  VkDescriptorSet s_gat      = SET(K_GAT_FWD, &raw, &in_gat, &ch0, &ch1, &ch2, &ch3, &params);
  VkDescriptorSet s_sigma    = SET(K_SIGMA_CFA, &in_gat, &params);
  VkDescriptorSet s_sig_h    = SET(K_SIGMA_HIST, &in_gat, &sigma_hist);
  VkDescriptorSet s_sig_f    = SET(K_SIGMA_FIN, &sigma_hist, &params);
  (void)s_sigma;   /* single-WG original kept for A/B; two-stage is production */
  VkDescriptorSet s_unified  = SET(K_UNIFIED, &params);
  VkDescriptorSet s_norm     = SET(K_NORMALIZE, &in_gat, &ch0, &ch1, &ch2, &ch3, &params);
  VkDescriptorSet s_dr_red   = SET(K_DR_REDUCE, &in_gat, &raw, &params, &part);
  VkDescriptorSet s_dr_fin   = SET(K_DR_FIN, &part, &params);
  VkDescriptorSet s_dr_rred  = SET(K_DR_RREDUCE, &in_gat, &raw, &params, &part_r);
  VkDescriptorSet s_dr_rfin  = SET(K_DR_RFIN, &part_r, &params);
  VkDescriptorSet s_dsub     = SET(K_DARK_SUB, &in_gat, &ch0, &ch1, &ch2, &ch3, &params);
  VkDescriptorSet s_fwdl     = SET(K_FWD_L, &in_gat, &L_cs);
  VkDescriptorSet s_cex      = SET(K_CHROMA_EX, &in_gat, &C1_h, &C2_h, &C3_h);
  /* [B6] fast-mode kernel selection (same buffer graph, swapped pipelines).
   * [B7g] quality 8x8 defaults to the subgroup-cooperative kernel when the
   * device can pin subgroup size 32; GALOSH_SG=0 forces the classic
   * transcription (A/B + belt-and-braces opt-out). */
  const char *sg_env = getenv("GALOSH_SG");
  const int use_sg = g_sg_ok && !(sg_env && sg_env[0] == '0');
  const int kid_p12 = (wht_block == 4) ? K_PASS12_W4
                                       : (use_sg ? K_PASS12_SG : K_PASS12);
  const int kid_up  = upsample_fast ? K_FASTUP : K_K16;
  /* [FP16 v1] the FINAL upsample site (K_O32_9) touches contract buffers
   * (Cden f16 in, L_pixel f16 guide, Cal f16 out) → needs the _f16 variant
   * of WHICHEVER upsampler --upsample selected; pyramid sites keep kid_up. */
  const int kid_up_final = upsample_fast ? K_FASTUP_F16 : K_K16_F16;
  VkDescriptorSet s_p12      = SET(kid_p12, &L_cs, &L_cs_den);
  VkDescriptorSet s_p1d      = SET(K_PASS1_DUMP, &L_cs, &pilot_dbg);
  VkDescriptorSet s_p6       = SET(K_P6_FUSED, &L_cs_den, &L_pixel, &L_h_den);
  VkDescriptorSet s_Lq       = SET(K_BOX2_H16, &L_h_den, &L_q);   /* [FP16 v1] 7a: f16 in */
  VkDescriptorSet s_Le       = SET(K_BOX2, &L_q, &L_e);           /* 7b: all f32 */
  VkDescriptorSet s_Cq       = SET(K_BOX2_3P, &C1_h, &C2_h, &C3_h, &C_q1, &C_q2, &C_q3);
  VkDescriptorSet s_Ce       = SET(K_BOX2_3P, &C_q1, &C_q2, &C_q3, &C_e1, &C_e2, &C_e3);
  VkDescriptorSet s_lo_h     = SET(K_LOESS_T_G16, &L_h_den, &C1_h, &C2_h, &C3_h, &Cl_h1, &Cl_h2, &Cl_h3);  /* [FP16 v1] 7e: f16 guide */
  VkDescriptorSet s_lo_q     = SET(K_LOESS_T, &L_q, &C_q1, &C_q2, &C_q3, &Cl_q1, &Cl_q2, &Cl_q3);
  VkDescriptorSet s_lo_e     = SET(K_LOESS_T, &L_e, &C_e1, &C_e2, &C_e3, &Cl_e1, &Cl_e2, &Cl_e3);
  VkDescriptorSet s_cropq    = SET(K_CROP_H16, &L_h_den, &L_for_q);   /* [FP16 v1] 7h: f16 in */
  VkDescriptorSet s_k16_q2h  = SET(kid_up, &Cl_q1, &Cl_q2, &Cl_q3, &L_for_q, &Cqup_r1, &Cqup_r2, &Cqup_r3);
  /* [B7f] 3-plane pads: one set per site instead of three (K_PAD stays
   * compiled but unused — deprecated single-plane original). */
  VkDescriptorSet s_pad_q3  = SET(K_PAD3, &Cqup_r1, &Cqup_r2, &Cqup_r3, &Cqup1, &Cqup2, &Cqup3);
  VkDescriptorSet s_crope    = SET(K_CROP, &L_q, &L_for_e);
  VkDescriptorSet s_k16_e2q  = SET(kid_up, &Cl_e1, &Cl_e2, &Cl_e3, &L_for_e, &Ceq_r1, &Ceq_r2, &Ceq_r3);
  VkDescriptorSet s_pad_e3  = SET(K_PAD3, &Ceq_r1, &Ceq_r2, &Ceq_r3, &Ceq1, &Ceq2, &Ceq3);
  VkDescriptorSet s_k16_eq2h = SET(kid_up, &Ceq1, &Ceq2, &Ceq3, &L_for_q, &Ceup_r1, &Ceup_r2, &Ceup_r3);
  VkDescriptorSet s_pad_eu3 = SET(K_PAD3, &Ceup_r1, &Ceup_r2, &Ceup_r3, &Ceup1, &Ceup2, &Ceup3);
  VkDescriptorSet s_smooth   = SET(K_SMOOTH, &C1_h, &C2_h, &C3_h, &Cl_h1, &Cl_h2, &Cl_h3,
                                   &Cqup1, &Cqup2, &Cqup3, &Ceup1, &Ceup2, &Ceup3,
                                   &Cden1, &Cden2, &Cden3);
  VkDescriptorSet s_k16_fin  = SET(kid_up_final, &Cden1, &Cden2, &Cden3, &L_pixel, &Cal1, &Cal2, &Cal3);  /* [FP16 v1] */
  VkDescriptorSet s_inv      = SET(K_INV, &L_pixel, &Cal1, &Cal2, &Cal3, &raw,
                                   &lut_d, &lut_x, &lut_p, &params);
  /* [B7f] fused final (common path; the unfused pair above serves the
   * GALOSH_DUMP_DIR path which must materialize p9_C1_aligned). */
  const int kid_fin_fused = upsample_fast ? K_FASTUP_INV_F : K_K16_INV_F;
  VkDescriptorSet s_fin_fused = SET(kid_fin_fused, &Cden1, &Cden2, &Cden3,
                                    &L_pixel, &raw, &lut_d, &lut_x, &lut_p, &params);

  PcW pc[8];
#define PCI(k, v) pc[k].i = (v)
#define PCF(k, v) pc[k].f = (v)

  /* [B5] stateful noise modes: load state + decide whether P0/LUT run. */
  float st_a = 0.0f, st_s = 0.0f; int st_n = -1;
  if(noise_state_path)
  {
    FILE *sf = fopen(noise_state_path, "r");
    if(sf)
    {
      if(fscanf(sf, "%f %f %d", &st_a, &st_s, &st_n) != 3) st_n = -1;
      /* optional 2nd line: learned banding rate (CPU exe ignores it) */
      float r = 0.0f;
      if(fscanf(sf, " rate %f", &r) == 1 && r > 0.0f) g_band_rate_ms = r;
      fclose(sf);
    }
  }
  const int have_state = (st_n >= 0 && st_a > 0.0f);
  int hold_frame = 0;   /* 1 = use stored model, skip P0 (+ LUT if cached) */
  float ema_beta = 0.0f;
  if(strcmp(noise_mode, "hold") == 0)
  {
    if(!have_state) { fprintf(stderr, "--noise=hold needs a valid --noise-state\n"); return 1; }
    hold_frame = 1;
  }
  else if(strncmp(noise_mode, "every:", 6) == 0)
  {
    const int N = atoi(noise_mode + 6);
    hold_frame = (have_state && st_n + 1 < (N > 0 ? N : 1));
  }
  else if(strncmp(noise_mode, "ema:", 4) == 0)
  {
    const float b = (float)atof(noise_mode + 4);
    ema_beta = (b > 0.0f && b <= 1.0f) ? b : 1.0f;
  }
  char lut_cache[1300] = { 0 };
  if(noise_state_path)
    snprintf(lut_cache, sizeof(lut_cache), "%s.lut", noise_state_path);

  /* ================= SEG A: Phase 0 ================= */
  VkCommandBuffer cb;
  if(!hold_frame)
  {
  cb = cb_begin();
  PCI(0, W); PCI(1, H); PCI(2, ne_n_bx); PCI(3, ne_n_by); PCI(4, ne_per_ch);
  dispatch_k(cb, K_NE_STATS, s_ne_stats, pc, 5,
             (uint32_t)AUP(ne_total, 64), 1, 1, "K_O32_P0a block_stats");
  PCI(0, W); PCI(1, H); PCI(2, (int)ne_total);
  dispatch_k(cb, K_NE_FIN, s_ne_fin, pc, 3, 1, 1, 1, "K_O32_P0b ne_finalize");
  vkCmdFillBuffer(cb, dt_hist.buf, 0, 4096 * 4, 0); barrier(cb);
  PCI(0, W); PCI(1, H);
  dispatch_k(cb, K_NE_DT_HIST, s_dt_hist, pc, 2,
             (uint32_t)AUP(hw_3, 16), (uint32_t)AUP(hh_3, 16), 1, "K_O32_P0e dark_thresh_hist");
  PCI(0, P_DARK_THRESH);
  dispatch_k(cb, K_NE_DT_FIN, s_dt_fin, pc, 1, 1, 1, 1, "K_O32_P0f dark_thresh_fin");
  vkCmdFillBuffer(cb, dl_hist.buf, 0, 4096 * 4, 0); barrier(cb);
  PCI(0, W); PCI(1, H); PCI(2, P_DARK_THRESH);
  dispatch_k(cb, K_NE_DL_HIST, s_dl_hist, pc, 3,
             (uint32_t)AUP(hw_a, 16), (uint32_t)AUP(hh_a, 16), 1, "K_O32_P0g dark_lap_hist");
  PCI(0, P_DARK_THRESH);
  dispatch_k(cb, K_NE_D_FIN, s_d_fin, pc, 1, 1, 1, 1, "K_O32_P0h dark_finalize");
  cb_submit_wait(cb);
  }  /* !hold_frame */

  /* HOST SYNC #1: decide (alpha, sigma_sq) — fit / ext CLI / state / ema.
   * [iGPU-opt] params writes are RECORDED into the merged SEG_BC command
   * buffer via vkCmdUpdateBuffer instead of 3 synchronous staging submits
   * (each waitIdle costs ~0.5-1 ms on a WDDM iGPU). */
  float alpha, sigma_sq;
  int   new_frames_since_fit = 0;
  int   pend_trio = 0;
  float trio_v[3];
  if(hold_frame)
  {
    alpha = st_a; sigma_sq = st_s;
    new_frames_since_fit = st_n + 1;
    trio_v[0] = alpha; trio_v[1] = sigma_sq;
    trio_v[2] = sigma_sq / fmaxf(alpha, 1e-12f);
    pend_trio = 1;
    fprintf(stderr, "[vk] noise HOLD (frames_since_fit=%d): alpha=%.8f sigma_sq=%.10f\n",
            new_frames_since_fit, alpha, sigma_sq);
  }
  else
  {
    float est[PARAMS_SIZE];
    download(&params, est, PARAMS_SIZE * 4, 0);
    alpha = est[P_ALPHA]; sigma_sq = est[P_SIGMA_SQ];
    if(ext_model) { alpha = alpha_ext; sigma_sq = sig_ext; }
    if(ema_beta > 0.0f && have_state)
    {
      alpha    = ema_beta * alpha    + (1.0f - ema_beta) * st_a;
      sigma_sq = ema_beta * sigma_sq + (1.0f - ema_beta) * st_s;
    }
    if(ext_model || ema_beta > 0.0f)
    {
      trio_v[0] = alpha; trio_v[1] = sigma_sq;
      trio_v[2] = sigma_sq / fmaxf(alpha, 1e-12f);
      pend_trio = 1;
    }
    fprintf(stderr, "[vk] Phase 0 noise est%s: alpha=%.6f sigma_sq=%.8f\n",
            ext_model ? " OVERRIDE" : (ema_beta > 0.0f ? " EMA" : ""), alpha, sigma_sq);
  }

  /* [B5] LUT: on hold frames try the disk cache (32 KB sidecar) before
   * paying the ~11 ms GPU rebuild.  Cache is (lut_d, lut_x, lut_params). */
  int lut_from_cache = 0;
  if(hold_frame && lut_cache[0])
  {
    FILE *cf = fopen(lut_cache, "rb");
    if(cf)
    {
      float *tmp = malloc((GAT_LUT_SIZE * 2 + 8) * 4);
      if(fread(tmp, 4, GAT_LUT_SIZE * 2 + 8, cf) == (size_t)(GAT_LUT_SIZE * 2 + 8))
      {
        /* stage all three regions once; copies are recorded in SEG_BC
         * (staging is not reused until after that CB completes). */
        memcpy((char *)g_stg_map,         tmp,                     GAT_LUT_SIZE * 4);
        memcpy((char *)g_stg_map + 16384, tmp + GAT_LUT_SIZE,      GAT_LUT_SIZE * 4);
        memcpy((char *)g_stg_map + 32768, tmp + 2 * GAT_LUT_SIZE,  8 * 4);
        lut_from_cache = 1;
      }
      free(tmp); fclose(cf);
    }
  }

  /* ============ SEG BC: Phase 1 + LUT + IRLS + P2b + P3/P4 =============
   * [iGPU-opt] merged into ONE submission: the pre-IRLS readback was
   * already redundant (blueprint note 9), s_init and the trio are known
   * at record time -> vkCmdUpdateBuffer inline.  Saves 4-6 waitIdle
   * round-trips per frame (~0.5-1 ms each on a WDDM iGPU). */
  cb = cb_begin();
  if(pend_trio)
  {
    vkCmdUpdateBuffer(cb, params.buf, P_ALPHA * 4,    4, &trio_v[0]);
    vkCmdUpdateBuffer(cb, params.buf, P_SIGMA_SQ * 4, 4, &trio_v[1]);
    vkCmdUpdateBuffer(cb, params.buf, P_S_SCALE * 4,  4, &trio_v[2]);
    barrier(cb);
  }
  if(lut_from_cache)
  {
    VkBufferCopy c1 = { .srcOffset = 0,     .dstOffset = 0, .size = GAT_LUT_SIZE * 4 };
    VkBufferCopy c2 = { .srcOffset = 16384, .dstOffset = 0, .size = GAT_LUT_SIZE * 4 };
    VkBufferCopy c3 = { .srcOffset = 32768, .dstOffset = 0, .size = 8 * 4 };
    vkCmdCopyBuffer(cb, g_stg.buf, lut_d.buf, 1, &c1);
    vkCmdCopyBuffer(cb, g_stg.buf, lut_x.buf, 1, &c2);
    vkCmdCopyBuffer(cb, g_stg.buf, lut_p.buf, 1, &c3);
    barrier(cb);
  }
  PCI(0, W); PCI(1, H);
  dispatch_k(cb, K_GAT_FWD, s_gat, pc, 2,
             (uint32_t)AUP(W, 16), (uint32_t)AUP(H, 16), 1, "K_O32_P1a gat_full");
  if(!lut_from_cache)
  {
    dispatch_k(cb, K_LUT_BUILD, s_lut, NULL, 0, GAT_LUT_SIZE / 256, 1, 1, "K_O32_P0c build_inv_lut");
    dispatch_k(cb, K_LUT_FIN, s_lut_fin, NULL, 0, 1, 1, 1, "K_O32_P0d lut_finalize");
  }
  PCI(0, W); PCI(1, H);
  /* [iGPU-opt] two-stage sigma_per_cfa (bit-identical to the single-WG
   * original: histogram counts are order-independent).  WG count scales
   * with the row count (size-based, not vendor-based): each WG carries a
   * fixed 2x4096-bin zero+flush overhead, so small frames use few WGs
   * (1080p: 2/ch) while 4K/8K get the parallelism (4-8/ch). */
  const int sigma_wg_per_ch = (hh / 512 > 8) ? 8 : (hh / 512 < 1 ? 1 : hh / 512);
  vkCmdFillBuffer(cb, sigma_hist.buf, 0, 4 * 4096 * 4, 0); barrier(cb);
  PCI(0, W); PCI(1, H); PCI(2, sigma_wg_per_ch);
  dispatch_k(cb, K_SIGMA_HIST, s_sig_h, pc, 3, (uint32_t)(4 * sigma_wg_per_ch), 1, 1,
             "K_O32_P1b sigma_hist");
  dispatch_k(cb, K_SIGMA_FIN, s_sig_f, NULL, 0, 1, 1, 1, "K_O32_P1b2 sigma_fin");
  dispatch_k(cb, K_UNIFIED, s_unified, NULL, 0, 1, 1, 1, "K_O32_P1c unified");
  PCI(0, W); PCI(1, H);
  dispatch_k(cb, K_NORMALIZE, s_norm, pc, 2,
             (uint32_t)AUP(W, 16), (uint32_t)AUP(H, 16), 1, "K_O32_P1d normalize");

  /* IRLS scale seed — known at record time (SYNC#1 values), inline update
   * (the OpenCL host's second readback was redundant: blueprint note 9). */
  const float a_for_s = fmaxf(alpha, 1e-12f);
  const float s_init  = sigma_sq / a_for_s;
  const float s_min   = 0.05f * s_init, s_max = 50.0f * s_init;
  vkCmdUpdateBuffer(cb, params.buf, P_S_SCALE * 4, 4, &s_init);
  barrier(cb);

  for(int it = 0; it <= 2; it++)
  {
    PCI(0, W); PCI(1, H);
    dispatch_k(cb, K_DR_REDUCE, s_dr_red, pc, 2, N_REDUCE_WG, 1, 1,
               it == 0 ? "K_O32_P2a_R0" : it == 1 ? "K_O32_P2a_R1" : "K_O32_P2a_R2");
    PCI(0, N_REDUCE_WG);
    dispatch_k(cb, K_DR_FIN, s_dr_fin, pc, 1, 1, 1, 1,
               it == 0 ? "K_O32_P2a_F0" : it == 1 ? "K_O32_P2a_F1" : "K_O32_P2a_F2");
    if(it == 2) break;
    PCI(0, W); PCI(1, H);
    dispatch_k(cb, K_DR_RREDUCE, s_dr_rred, pc, 2, N_REDUCE_WG, 1, 1,
               it == 0 ? "K_O32_P2a_RR0" : "K_O32_P2a_RR1");
    PCI(0, N_REDUCE_WG); PCF(1, s_min); PCF(2, s_max);
    dispatch_k(cb, K_DR_RFIN, s_dr_rfin, pc, 3, 1, 1, 1,
               it == 0 ? "K_O32_P2a_RF0" : "K_O32_P2a_RF1");
  }
  PCI(0, W); PCI(1, H);
  dispatch_k(cb, K_DARK_SUB, s_dsub, pc, 2,
             (uint32_t)AUP(W, 16), (uint32_t)AUP(H, 16), 1, "K_O32_P2b dark_sub");
  dispatch_k(cb, K_FWD_L, s_fwdl, pc, 2,
             (uint32_t)AUP(W, 16), (uint32_t)AUP(H, 16), 1, "K_O32_2 fwd_L_cs");
  PCI(0, W); PCI(1, H); PCI(2, hw); PCI(3, hh);
  dispatch_k(cb, K_CHROMA_EX, s_cex, pc, 4,
             (uint32_t)AUP(hw, 16), (uint32_t)AUP(hh, 16), 1, "K_O32_3 chroma_extract");
  cb_submit_wait(cb);
  /* pass12 = per-band submissions (TDR-safe on slow GPUs, see helper) */
  PCI(0, W); PCI(1, H); PCF(2, strength * luma_str); PCI(3, phase_stride);
  dispatch_k_banded(kid_p12, s_p12, pc, 4,
             (uint32_t)AUP(W, O32_TILE), (uint32_t)AUP(H, O32_TILE), "K_O32_5 pass12_L_fr");
  cb = cb_begin();
  PCI(0, W); PCI(1, H); PCI(2, hw);
  dispatch_k(cb, K_P6_FUSED, s_p6, pc, 3,
             (uint32_t)AUP(W, 16), (uint32_t)AUP(H, 16), 1, "K_O32_6 L_pixel+L_h_den_fused");
  /* [iGPU-opt] P6 and SEG_D share one submission on the common path;
   * dump/small-image paths split as needed. */
  const int need_split = (dump_dir != NULL) ||
                         (cq_w < 4 || cq_h < 4 || ce_w < 4 || ce_h < 4);
  if(need_split) cb_submit_wait(cb);

  if(dump_dir)
  {
    dump_buf(dump_dir, "p2_in_gat", &in_gat, npix);
    dump_buf_f16(dump_dir, "p3_L_cs", &L_cs, npix);          /* [FP16 v1] */
    dump_buf(dump_dir, "p4_C1_h", &C1_h, chsize);
    dump_buf(dump_dir, "p4_C2_h", &C2_h, chsize);
    dump_buf(dump_dir, "p4_C3_h", &C3_h, chsize);
    dump_buf_f16(dump_dir, "p5_L_cs_den", &L_cs_den, npix);  /* [FP16 v1] */
    dump_buf_f16(dump_dir, "p6_L_pixel", &L_pixel, npix);    /* [FP16 v1] */
    dump_buf_f16(dump_dir, "p6_L_h_den", &L_h_den, chsize);  /* [FP16 v1] */
    /* pilot dump: extra dispatch (matches OpenCL debug path) */
    PCI(0, W); PCI(1, H); PCF(2, strength * luma_str);
    dispatch_k_banded(K_PASS1_DUMP, s_p1d, pc, 3,
               (uint32_t)AUP(W, O32_TILE), (uint32_t)AUP(H, O32_TILE), "pass1_dump");
    dump_buf(dump_dir, "p5_pilot", &pilot_dbg, npix);
  }

  /* small-image guard (blueprint trap #8).  [FP16 v1] L_pixel is f16 now:
   * a raw device copy into the f32 `raw` buffer would misinterpret bits,
   * so download the f16 buffer and widen on the host instead. */
  if(cq_w < 4 || cq_h < 4 || ce_w < 4 || ce_h < 4)
  {
    uint16_t *h16 = malloc(full_h16);
    download(&L_pixel, h16, full_h16, 0);
    for(size_t i = 0; i < npix; i++) img[i] = half_to_float(h16[i]);
    free(h16);
    goto finish;
  }

  /* ================= SEG D: P7 pyramid + P8-10 ================= */
  if(need_split) cb = cb_begin();   /* common path: continue the P6 CB */
  PCI(0, hw); PCI(1, hh);
  dispatch_k(cb, K_BOX2_H16, s_Lq, pc, 2,
             (uint32_t)AUP(cq_w, 16), (uint32_t)AUP(cq_h, 16), 1, "K_O32_7a L_q");
  PCI(0, cq_w); PCI(1, cq_h);
  dispatch_k(cb, K_BOX2, s_Le, pc, 2,
             (uint32_t)AUP(ce_w, 16), (uint32_t)AUP(ce_h, 16), 1, "K_O32_7b L_e");
  PCI(0, hw); PCI(1, hh);
  dispatch_k(cb, K_BOX2_3P, s_Cq, pc, 2,
             (uint32_t)AUP(cq_w, 16), (uint32_t)AUP(cq_h, 16), 1, "K_O32_7c C_q");
  PCI(0, cq_w); PCI(1, cq_h);
  dispatch_k(cb, K_BOX2_3P, s_Ce, pc, 2,
             (uint32_t)AUP(ce_w, 16), (uint32_t)AUP(ce_h, 16), 1, "K_O32_7d C_e");
  /* LOESS: strength_c pushed EXPLICITLY at all 3 scales (blueprint trap #2) */
  PCI(0, hw); PCI(1, hh); PCF(2, 1.0f);
  dispatch_k(cb, K_LOESS_T_G16, s_lo_h, pc, 3,
             (uint32_t)AUP(hw, 24), (uint32_t)AUP(hh, 24), 1, "K_O32_7e LOESS_h_t");
  PCI(0, cq_w); PCI(1, cq_h); PCF(2, 1.0f);
  dispatch_k(cb, K_LOESS_T, s_lo_q, pc, 3,
             (uint32_t)AUP(cq_w, 24), (uint32_t)AUP(cq_h, 24), 1, "K_O32_7f LOESS_q_t");
  PCI(0, ce_w); PCI(1, ce_h); PCF(2, 1.0f);
  dispatch_k(cb, K_LOESS_T, s_lo_e, pc, 3,
             (uint32_t)AUP(ce_w, 24), (uint32_t)AUP(ce_h, 24), 1, "K_O32_7g LOESS_e_t");
  PCI(0, hw); PCI(1, hh); PCI(2, kq_w); PCI(3, kq_h);
  dispatch_k(cb, K_CROP_H16, s_cropq, pc, 4,
             (uint32_t)AUP(kq_w, 16), (uint32_t)AUP(kq_h, 16), 1, "K_O32_7h crop_L_for_q");
  PCI(0, cq_w); PCI(1, cq_h); PCF(2, 1.5f);
  dispatch_k(cb, kid_up, s_k16_q2h, pc, 3,
             (uint32_t)AUP(kq_w, 16), (uint32_t)AUP(kq_h, 16), 1, "K_O32_7i K16_q2h");
  PCI(0, kq_w); PCI(1, kq_h); PCI(2, hw); PCI(3, hh);
  dispatch_k(cb, K_PAD3, s_pad_q3, pc, 4,
             (uint32_t)AUP(hw, 16), (uint32_t)AUP(hh, 16), 1, "K_O32_7j pad_q_up_3p");
  PCI(0, cq_w); PCI(1, cq_h); PCI(2, ke_w); PCI(3, ke_h);
  dispatch_k(cb, K_CROP, s_crope, pc, 4,
             (uint32_t)AUP(ke_w, 16), (uint32_t)AUP(ke_h, 16), 1, "K_O32_7k crop_L_for_e");
  PCI(0, ce_w); PCI(1, ce_h); PCF(2, 1.5f);
  dispatch_k(cb, kid_up, s_k16_e2q, pc, 3,
             (uint32_t)AUP(ke_w, 16), (uint32_t)AUP(ke_h, 16), 1, "K_O32_7l K16_e2q");
  PCI(0, ke_w); PCI(1, ke_h); PCI(2, cq_w); PCI(3, cq_h);
  dispatch_k(cb, K_PAD3, s_pad_e3, pc, 4,
             (uint32_t)AUP(cq_w, 16), (uint32_t)AUP(cq_h, 16), 1, "K_O32_7m pad_e_to_q_3p");
  PCI(0, cq_w); PCI(1, cq_h); PCF(2, 1.5f);
  dispatch_k(cb, kid_up, s_k16_eq2h, pc, 3,
             (uint32_t)AUP(kq_w, 16), (uint32_t)AUP(kq_h, 16), 1, "K_O32_7n K16_q2h_e");
  PCI(0, kq_w); PCI(1, kq_h); PCI(2, hw); PCI(3, hh);
  dispatch_k(cb, K_PAD3, s_pad_eu3, pc, 4,
             (uint32_t)AUP(hw, 16), (uint32_t)AUP(hh, 16), 1, "K_O32_7o pad_e_up_3p");
  PCI(0, hw); PCI(1, hh); PCF(2, strength * chroma_str);
  dispatch_k(cb, K_SMOOTH, s_smooth, pc, 3,
             (uint32_t)AUP(hw, 16), (uint32_t)AUP(hh, 16), 1, "K_O32_8 smoothstep");
  if(dump_dir)
  {
    /* unfused pair: materializes Cal1..3 for the p9 dump */
    PCI(0, hw); PCI(1, hh); PCF(2, 1.5f);
    dispatch_k(cb, kid_up_final, s_k16_fin, pc, 3,
               (uint32_t)AUP(W, 16), (uint32_t)AUP(H, 16), 1, "K_O32_9 K16_final");
    PCI(0, W); PCI(1, H);
    dispatch_k(cb, K_INV, s_inv, pc, 2,
               (uint32_t)AUP(W, 16), (uint32_t)AUP(H, 16), 1, "K_O32_10 inverse_final");
  }
  else
  {
    /* [B7f] fused final: f16 contract round in-register, bit-identical */
    PCI(0, hw); PCI(1, hh); PCF(2, 1.5f);
    dispatch_k(cb, kid_fin_fused, s_fin_fused, pc, 3,
               (uint32_t)AUP(W, 16), (uint32_t)AUP(H, 16), 1, "K_O32_9+10 fused_final");
  }
  cb_submit_wait(cb);

  if(dump_dir)
  {
    dump_buf(dump_dir, "p7_C1_loess_h", &Cl_h1, chsize);
    dump_buf(dump_dir, "p7_C1_q_up", &Cqup1, chsize);
    dump_buf(dump_dir, "p7_C1_e_up", &Ceup1, chsize);
    dump_buf_f16(dump_dir, "p8_C1_h_den", &Cden1, chsize);   /* [FP16 v1] */
    dump_buf_f16(dump_dir, "p9_C1_aligned", &Cal1, npix);    /* [FP16 v1] */
  }

  download(&raw, img, full_f, 0);
finish:
  memcpy(output, img, full_f);

  /* profiling report */
  if(g_verbose && g_ts_n)
  {
    uint64_t ts[256];
    CHECK(vkGetQueryPoolResults(g_dev, g_qpool, 0, g_ts_n, sizeof(uint64_t) * g_ts_n,
                                ts, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
    double total = 0;
    fprintf(stderr, "[vk] ====== PER-KERNEL PROFILING ======\n");
    for(uint32_t i = 0; i + 1 < g_ts_n; i += 2)
    {
      double ms = (double)(ts[i + 1] - ts[i]) * g_props.limits.timestampPeriod * 1e-6;
      total += ms;
      fprintf(stderr, "[vk]   %-32s %8.3f ms\n", g_ts_label[i], ms);
    }
    fprintf(stderr, "[vk]   TOTAL (GPU time)  %.3f ms\n", total);
  }
  free(img);
  vkDeviceWaitIdle(g_dev);
  if(g_stg_map) { vkUnmapMemory(g_dev, g_stg.mem); g_stg_map = NULL; }
  galosh_destroy_live_bufs();
  return 0;
}

void galosh_vulkan_set_shader_dir(const char *dir)
{
  if(!dir) { g_shader_dir[0] = 0; return; }
  strncpy(g_shader_dir, dir, sizeof(g_shader_dir) - 1);
  g_shader_dir[sizeof(g_shader_dir) - 1] = 0;
}

static int g_vk_available = -1;

int galosh_vulkan_available(void)
{
  if(g_vk_available >= 0) return g_vk_available;

  if(!g_shader_dir[0])
  {
    const char *e = getenv("MLVAPP_GALOSH_SHADER_DIR");
    if(!e) e = getenv("GALOSH_VK_SHADER_DIR");
    if(e) galosh_vulkan_set_shader_dir(e);
  }

  /* Do not let a missing shader directory kill the process later. */
  {
    char probe[1200];
    snprintf(probe, sizeof(probe), "%s/o32_pass12.spv",
             g_shader_dir[0] ? g_shader_dir : "shaders");
#ifdef _WIN32
    if(_access(probe, 0) != 0) { g_vk_available = 0; return 0; }
#else
    if(access(probe, R_OK) != 0) { g_vk_available = 0; return 0; }
#endif
  }

#ifdef __APPLE__
  galosh_vk_prepare_macos_icd();
#endif

  VkInstance inst = VK_NULL_HANDLE;
  if(galosh_vk_create_instance(&inst) != VK_SUCCESS)
  {
    g_vk_available = 0;
    return 0;
  }
  uint32_t n = 0;
  VkResult r = vkEnumeratePhysicalDevices(inst, &n, NULL);
  VkPhysicalDevice pds[8];
  if(n > 8) n = 8;
  if(r == VK_SUCCESS && n > 0)
    r = vkEnumeratePhysicalDevices(inst, &n, pds);

  int usable = 0;
  if(r == VK_SUCCESS)
  {
    for(uint32_t i = 0; i < n; i++)
    {
      if(galosh_vk_supports_f16(pds[i])) { usable = 1; break; }
    }
  }
  vkDestroyInstance(inst, NULL);
  g_vk_available = usable ? 1 : 0;
  return g_vk_available;
}

int galosh_vulkan_denoise(const float *in, float *out,
                          int width, int height,
                          float luma_strength, float chroma_strength,
                          int fast,
                          float alpha, float sigma_sq,
                          int have_noise_model)
{
  return galosh_vk_run(in, out, width, height, 1.0f,
                       luma_strength, chroma_strength,
                       alpha, sigma_sq, have_noise_model,
                       fast ? 4 : 8, fast ? 1 : 0);
}

void galosh_vulkan_shutdown(void)
{
  if(g_dev)
  {
    vkDeviceWaitIdle(g_dev);
    galosh_destroy_live_bufs();
    for(int i = 0; i < K_COUNT; i++)
    {
      if(g_k[i].pipe) vkDestroyPipeline(g_dev, g_k[i].pipe, NULL);
      if(g_k[i].pl) vkDestroyPipelineLayout(g_dev, g_k[i].pl, NULL);
      if(g_k[i].dsl) vkDestroyDescriptorSetLayout(g_dev, g_k[i].dsl, NULL);
    }
    if(g_qpool) vkDestroyQueryPool(g_dev, g_qpool, NULL);
    if(g_dpool) vkDestroyDescriptorPool(g_dev, g_dpool, NULL);
    if(g_pool) vkDestroyCommandPool(g_dev, g_pool, NULL);
    vkDestroyDevice(g_dev, NULL);
    g_dev = VK_NULL_HANDLE;
  }
  if(g_inst)
  {
    vkDestroyInstance(g_inst, NULL);
    g_inst = VK_NULL_HANDLE;
  }
}
