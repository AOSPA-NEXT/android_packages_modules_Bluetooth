/*
 * Copyright 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#ifndef TARGET_FLOSS

#include <a2dp.sysprop.h>
#include <avrcp.sysprop.h>
#include <ble.sysprop.h>
#include <bta.sysprop.h>
#include <device_id.sysprop.h>
#include <hfp.sysprop.h>

#define GET_SYSPROP(namespace, prop, default) \
  android::sysprop::bluetooth::namespace ::prop().value_or(default)

#else

#define GET_SYSPROP(namespace, prop, default) default

#endif