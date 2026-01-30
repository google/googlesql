/*
 * Copyright 2019 Google LLC
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
 *
 */

package com.google.googlesql;

import com.google.googlesql.FunctionProtos.ResolvedFunctionCallInfoProto;
import java.io.Serializable;
import java.util.List;

/** TODO: Add implementation. */
public class ResolvedFunctionCallInfo implements Serializable {
  public ResolvedFunctionCallInfo() {}

  public static ResolvedFunctionCallInfo deserialize(
      ResolvedFunctionCallInfoProto proto, List<? extends DescriptorPool> pools) {
    return new ResolvedFunctionCallInfo();
  }

  @Override
  public String toString() {
    return "ResolvedFunctionCallInfo";
  }

  public boolean isDefaultValue() {
    return true;
  }
}
