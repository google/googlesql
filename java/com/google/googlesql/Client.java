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

/** Provides interface for accessing the client. */
final class Client {
  private static GoogleSqlLocalServiceGrpc.GoogleSqlLocalServiceBlockingStub stub = null;

  private Client() {}

  /** Returns the stub that can be used to call RPC of the GoogleSQL server. */
  static synchronized GoogleSqlLocalServiceGrpc.GoogleSqlLocalServiceBlockingStub getStub() {
    if (stub == null) {
      stub = GoogleSqlLocalServiceGrpc.newBlockingStub(ClientChannelProvider.loadChannel());
    }
    return stub;
  }
}
