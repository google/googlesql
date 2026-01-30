//
// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "googlesql/local_service/local_service_jni.h"

#include <errno.h>
#include <fcntl.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_posix.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <memory>

#include "googlesql/local_service/local_service_grpc.h"

namespace googlesql {
namespace local_service {
namespace {

static grpc::Server* GetServer() {
  static grpc::Server* server = []() {
    // The service must remain for the lifetime of the server.
    static GoogleSqlLocalServiceGrpcImpl* service =
        new GoogleSqlLocalServiceGrpcImpl();
    grpc::ServerBuilder builder;
    builder.RegisterService(service);
    return builder.BuildAndStart().release();
  }();
  return server;
}

static void ErrnoSocketException(JNIEnv* env) {
  char buf[128];
#if __USE_GNU
  char* outstr = strerror_r(errno, buf, sizeof(buf));
#else
  strerror_r(errno, buf, sizeof(buf));
  char* outstr = buf;
#endif

  jclass e = env->FindClass("java/net/SocketException");
  if (e == nullptr) {
    return;
  }
  env->ThrowNew(e, outstr);
}

static int GetSocketFd(JNIEnv* env) {
  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
    ErrnoSocketException(env);
    return -1;
  }

  for (int i = 0; i < 2; i++) {
    int flags = fcntl(sv[i], F_GETFL);
    if (flags == -1) {
      ErrnoSocketException(env);
      return -1;
    }

    if (fcntl(sv[i], F_SETFL, flags | O_NONBLOCK) < 0) {
      ErrnoSocketException(env);
      return -1;
    }
  }

  // gRPC takes ownership of the first fd
  grpc::AddInsecureChannelFromFd(GetServer(), sv[0]);
  return sv[1];
}

static jobject WrapFileDescriptor(JNIEnv* env, const int fd) {
  jclass ioutil = env->FindClass("sun/nio/ch/IOUtil");
  if (ioutil == nullptr) {
    return nullptr;
  }

  jmethodID newfd =
      env->GetStaticMethodID(ioutil, "newFD", "(I)Ljava/io/FileDescriptor;");
  if (newfd == nullptr) {
    return nullptr;
  }

  jobject javafd = env->CallStaticObjectMethod(ioutil, newfd, fd);
  if (javafd == nullptr) {
    return nullptr;
  }
  return javafd;
}

}  // namespace

jobject GetSocketChannel(JNIEnv* env) {
  jclass impl = env->FindClass("sun/nio/ch/SocketChannelImpl");
  if (impl == nullptr) {
    return nullptr;
  }

  jmethodID constructor =
      env->GetMethodID(impl, "<init>",
                       "(Ljava/nio/channels/spi/SelectorProvider;"
                       "Ljava/io/FileDescriptor;"
                       "Ljava/net/InetSocketAddress;)V");
  jmethodID constructor17;
  jobject spfinet;
  if (constructor == nullptr) {
    env->ExceptionClear();
    constructor17 = env->GetMethodID(impl, "<init>",
                                     "(Ljava/nio/channels/spi/SelectorProvider;"
                                     "Ljava/net/"
                                     "ProtocolFamily;"
                                     "Ljava/io/FileDescriptor;"
                                     "Ljava/net/SocketAddress;)V");
    if (constructor17 == nullptr) {
      return nullptr;
    }

    jclass spf = env->FindClass("java/net/StandardProtocolFamily");
    if (spf == nullptr) {
      return nullptr;
    }

    jfieldID fieldId =
        env->GetStaticFieldID(spf, "INET", "Ljava/net/StandardProtocolFamily;");
    if (fieldId == nullptr) {
      return nullptr;
    }

    spfinet = env->GetStaticObjectField(spf, fieldId);
    if (spfinet == nullptr) {
      return nullptr;
    }
  }

  int fd = GetSocketFd(env);
  if (fd == -1) {
    return nullptr;
  }

  jobject jfd = WrapFileDescriptor(env, fd);
  if (jfd == nullptr) {
    close(fd);
    return nullptr;
  }

  // java SocketChannel takes ownership of fd on success.
  jobject sc;
  if (constructor != nullptr) {
    sc = env->NewObject(impl, constructor, nullptr, jfd, nullptr);
  } else {
    sc = env->NewObject(impl, constructor17, nullptr, spfinet, jfd, nullptr);
  }
  if (sc == nullptr) {
    close(fd);
    return nullptr;
  }
  return sc;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad_googlesql_local_service(JavaVM* vm,
                                                           void* reserved) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv((void**)&env, JNI_VERSION_1_1) != JNI_OK) {
    return -1;
  }

  jclass system = env->FindClass("java/lang/System");
  if (system == nullptr) {
    return -1;
  }

  jmethodID gp = env->GetStaticMethodID(
      system, "getProperty", "(Ljava/lang/String;)Ljava/lang/String;");
  if (gp == nullptr) {
    return -1;
  }

  jstring classname = (jstring)env->CallStaticObjectMethod(
      system, gp, env->NewStringUTF("googlesql.local_service.class"));
  if (classname == nullptr) {
    return -1;
  }

  const char* classnamestr = env->GetStringUTFChars(classname, nullptr);
  jclass clazz = env->FindClass(classnamestr);
  env->ReleaseStringUTFChars(classname, classnamestr);
  classnamestr = nullptr;
  if (clazz == nullptr) {
    return -1;
  }

  static JNINativeMethod methods[] = {
      {(char*)"getSocketChannel", (char*)"()Ljava/nio/channels/SocketChannel;",
       (void*)GetSocketChannel},
  };
  if (env->RegisterNatives(clazz, methods,
                           sizeof(methods) / sizeof(JNINativeMethod)) !=
      JNI_OK) {
    return -1;
  }

  return env->GetVersion();
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
  return JNI_OnLoad_googlesql_local_service(vm, reserved);
}

}  // namespace local_service
}  // namespace googlesql
