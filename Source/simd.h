#ifndef SIMD_H
#define SIMD_H

#if defined(USE_SIMD)
#if defined(USE_AVX2) || defined(USE_AVX512)
#include <immintrin.h>
#elif defined(USE_NEON)
#include <arm_neon.h>
#define vmull_low_s16(a, b) vmull_s16(vget_low_s16(a), vget_low_s16(b))
#endif
#endif

#if defined(USE_AVX512)
typedef __m512i veci_t;
typedef __m512 vecf_t;

static inline veci_t zero(void) { return _mm512_setzero_si512(); }
static inline veci_t load(const int16_t *memory_address) {
  return _mm512_loadu_si512((const veci_t *)memory_address);
}
static inline veci_t set_epi16(int num) { return _mm512_set1_epi16(num); }
static inline veci_t set_epi32(int num) { return _mm512_set1_epi32(num); }
static inline vecf_t set_ps1(float num) { return _mm512_set1_ps(num); }
static inline veci_t slli_epi16(veci_t vector, int shift) {
  return _mm512_slli_epi16(vector, shift);
}
static inline veci_t mulhi_epi16(veci_t shift, veci_t vector) {
  return _mm512_mulhi_epi16(shift, vector);
}
static inline veci_t packus_epi16(veci_t vec1, veci_t vec2) {
  veci_t temp = _mm512_packus_epi16(vec1, vec2);
  return _mm512_permutexvar_epi64(_mm512_setr_epi64(0, 2, 4, 6, 1, 3, 5, 7),
                                    temp);
}
static inline void vec_store_i(veci_t *scalar, veci_t integer) {
  _mm512_store_si512(scalar, integer);
}
#if defined(__AVX512VNNI__)
inline veci_t dpbusd_epi32(veci_t sum, veci_t u, veci_t i) {
  return _mm512_dpbusd_epi32(sum, u, i);
}
#else
inline veci_t dpbusd_epi32(veci_t sum, veci_t u, veci_t i) {
  veci_t sum32 = _mm512_madd_epi16(_mm512_maddubs_epi16(u, i), _mm512_set1_epi16(1));
  return _mm512_add_epi32(sum32, sum);
}
#endif
static inline veci_t add_epi32(veci_t v1, veci_t v2) {
  return _mm512_add_epi32(v1, v2);
}
static inline veci_t multiply_epi16(veci_t v1, veci_t v2) {
  return _mm512_mullo_epi16(v1, v2);
}
static inline veci_t multiply_add_epi16(veci_t v1, veci_t v2) {
  return _mm512_madd_epi16(v1, v2);
}
static inline veci_t min_epi16(veci_t vector, veci_t quant) {
  return _mm512_min_epi16(vector, quant);
}
static inline veci_t clip_epi16(veci_t vector, veci_t zero, veci_t quant) {
  return _mm512_min_epi16(_mm512_max_epi16(vector, zero), quant);
}
static inline vecf_t cvtepi32_ps(veci_t vec) { return _mm512_cvtepi32_ps(vec); }
static inline vecf_t add_ps(vecf_t vec, vecf_t vec1) {
  return _mm512_add_ps(vec, vec1);
}
static inline vecf_t mul_ps(vecf_t vec, vecf_t vec1) {
  return _mm512_mul_ps(vec, vec1);
}
static inline vecf_t clip_ps(vecf_t vec, vecf_t max, vecf_t min) {
  return _mm512_max_ps(_mm512_min_ps(vec, max), min);
}
static inline vecf_t fmadd_ps(vecf_t a, vecf_t b, vecf_t c) {
  return _mm512_fmadd_ps(a, b, c);
}

static inline float reduce_add_ps(vecf_t *v) {
  return _mm512_reduce_add_ps(v[0]);
}

#elif defined(USE_AVX2)
typedef __m256i veci_t;
typedef __m256 vecf_t;

static inline veci_t zero(void) { return _mm256_setzero_si256(); }
static inline veci_t load(const int16_t *memory_address) {
  return _mm256_loadu_si256((const veci_t *)memory_address);
}
static inline veci_t set_epi16(int num) { return _mm256_set1_epi16(num); }
static inline veci_t set_epi32(int num) { return _mm256_set1_epi32(num); }
static inline vecf_t set_ps1(float num) { return _mm256_set1_ps(num); }
static inline veci_t slli_epi16(veci_t vector, int shift) {
  return _mm256_slli_epi16(vector, shift);
}
static inline veci_t mulhi_epi16(veci_t shift, veci_t vector) {
  return _mm256_mulhi_epi16(shift, vector);
}
static inline veci_t packus_epi16(veci_t vec1, veci_t vec2) {
  veci_t temp = _mm256_packus_epi16(vec1, vec2);
  return _mm256_permute4x64_epi64(temp, _MM_SHUFFLE(3, 1, 2, 0));
}
static inline void vec_store_i(veci_t *scalar, veci_t integer) {
  _mm256_store_si256(scalar, integer);
}
static inline veci_t dpbusd_epi32(veci_t sum, veci_t u, veci_t i) {
  veci_t sum32 =
      _mm256_madd_epi16(_mm256_maddubs_epi16(u, i), _mm256_set1_epi16(1));
  return _mm256_add_epi32(sum, sum32);
}
static inline veci_t add_epi32(veci_t v1, veci_t v2) {
  return _mm256_add_epi32(v1, v2);
}
static inline veci_t multiply_epi16(veci_t v1, veci_t v2) {
  return _mm256_mullo_epi16(v1, v2);
}
static inline veci_t multiply_add_epi16(veci_t v1, veci_t v2) {
  return _mm256_madd_epi16(v1, v2);
}
static inline veci_t min_epi16(veci_t vector, veci_t quant) {
  return _mm256_min_epi16(vector, quant);
}
static inline veci_t clip_epi16(veci_t vector, veci_t zero, veci_t quant) {
  return _mm256_min_epi16(_mm256_max_epi16(vector, zero), quant);
}
static inline vecf_t cvtepi32_ps(veci_t vec) { return _mm256_cvtepi32_ps(vec); }
static inline vecf_t add_ps(vecf_t vec, vecf_t vec1) {
  return _mm256_add_ps(vec, vec1);
}
static inline vecf_t mul_ps(vecf_t vec, vecf_t vec1) {
  return _mm256_mul_ps(vec, vec1);
}
static inline vecf_t clip_ps(vecf_t vec, vecf_t max, vecf_t min) {
  return _mm256_max_ps(_mm256_min_ps(vec, max), min);
}
static inline vecf_t fmadd_ps(vecf_t a, vecf_t b, vecf_t c) {
  return _mm256_fmadd_ps(a, b, c);
}

static inline float reduce_add_ps(vecf_t *v) {
  v[0] = _mm256_add_ps(v[0], v[1]);
  __m128 high = _mm256_extractf128_ps(v[0], 1);
  __m128 low = _mm256_castps256_ps128(v[0]);
  __m128 sum = _mm_add_ps(high, low);
  __m128 high64 = _mm_movehl_ps(sum, sum);
  __m128 sum64 = _mm_add_ps(sum, high64);

  return ((float *)&sum64)[0] + ((float *)&sum64)[1];
}

#elif defined(USE_NEON)
typedef int16x8_t vepi16;
typedef int32x4_t vepi32;

static inline vepi16 zero_epi16(void) { return vdupq_n_s16(0); }
static inline vepi32 zero_epi32(void) { return vdupq_n_s32(0); }
static inline vepi16 load_epi16(const int16_t *memory_address) {
  return vld1q_s16((const int16_t *)memory_address);
}
static inline vepi32 load_epi32(const int32_t *memory_address) {
  return vld1q_s32((const int32_t *)memory_address);
}
static inline vepi16 load_epi16_broadcast(int num) { return vdupq_n_s16(num); }
static inline vepi32 load_epi32_broadcast(int num) { return vdupq_n_s32(num); }
// static inline void store_epi16(void *memory_address, vepi16 vector) {
// _mm256_store_si256(memory_address, vector); }
static inline vepi32 add_epi32(vepi32 v1, vepi32 v2) {
  return vaddq_s32(v1, v2);
}
static inline vepi16 multiply_epi16(vepi16 v1, vepi16 v2) {
  return vmulq_s16(v1, v2);
}
static inline vepi32 multiply_add_epi16(vepi16 v1, vepi16 v2) {
  const vepi16 low = vmull_low_s16(v1, v2);
  const vepi32 high = vmull_high_s16(v1, v2);
  return vpaddq_s32(low, high);
}
static inline vepi16 clip(vepi16 vector, int l1q) {
  return vminq_s16(vmaxq_s16(vector, zero_epi16()), load_epi16_broadcast(l1q));
}
static inline int reduce_add_epi32(vepi32 v) {
  int32x2_t sum1 = vpadd_s32(vget_low_s32(v), vget_high_s32(v));
  int32x2_t sum2 = vpadd_s32(sum1, sum1);
  return vget_lane_s32(sum2, 0);
}

#endif
#endif
