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

import com.google.auto.value.AutoValue;
import java.io.Serializable;

/**
 * This class is for storing the start and end byte offsets of a symbol string as it occurs in a
 * bigger string. This class corresponds to the ParseLocationRange class in C++:
 */
@AutoValue
public abstract class ParseLocationRange implements Serializable {
  public static ParseLocationRange create(String fileName, int start, int end) {
    return new AutoValue_ParseLocationRange(fileName, start, end);
  }

  /* Name of the file containing the parsed string. */
  public abstract String fileName();

  /* Byte offset of the first character of the parsed string. */
  public abstract int start();

  /* Byte offset of the character after the end of the parsed string. */
  public abstract int end();

  public ParseLocationRangeProto serialize() {
    ParseLocationRangeProto.Builder builder = ParseLocationRangeProto.newBuilder();
    builder.setFilename(fileName());
    builder.setStart(start());
    builder.setEnd(end());
    return builder.build();
  }

  public static ParseLocationRange deserialize(ParseLocationRangeProto proto) {
    return create(proto.getFilename(), proto.getStart(), proto.getEnd());
  }
}
