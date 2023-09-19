/*
 * SPDX-FileCopyrightText: Copyright (c) 1993-2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TRT_KERNEL_H
#define TRT_KERNEL_H

#include "plugin.h"
#include "cublas_v2.h"
#include <algorithm>
#include <cassert>
#include <cstdio>

using namespace nvinfer1;
using namespace nvinfer1::plugin;
#define DEBUG_ENABLE 1

typedef enum
{
    NCHW = 0,
    NC4HW = 1,
    NC32HW = 2
} DLayout_t;
#ifndef TRT_RPNLAYER_H


#endif // TRT_RPNLAYER_H
#endif
