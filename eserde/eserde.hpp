/**
 ******************************************************************************
 * @file           : eserde.hpp
 * @brief          : eserde 汇总头：按 ESERDE_ENABLE_* 把要用的格式拉进来
 * @attention      : 单个格式头永远可以单独 include（<eserde/json.hpp> / <eserde/cbor.hpp>）——
 *                   那才是最精确的开关，不 include 就一点体积都不占。这个头只服务
 *                   "由构建系统集中配置"的场景：-DESERDE_ENABLE_CBOR=1 就多一个格式。
 *                   默认只开 JSON（缺省 = 不替你决定）。
 * @date           : 26-10-02
 ******************************************************************************
 */

#ifndef ESERDE_ESERDE_HPP
#define ESERDE_ESERDE_HPP

#ifndef ESERDE_ENABLE_JSON
#define ESERDE_ENABLE_JSON 1
#endif

#ifndef ESERDE_ENABLE_CBOR
#define ESERDE_ENABLE_CBOR 0
#endif

#if ESERDE_ENABLE_JSON
#include <eserde/json.hpp>
#endif

#if ESERDE_ENABLE_CBOR
#include <eserde/cbor.hpp>
#endif

#endif  // ESERDE_ESERDE_HPP
