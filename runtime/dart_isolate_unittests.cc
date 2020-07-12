// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/fml/mapping.h"
#include "flutter/fml/synchronization/count_down_latch.h"
#include "flutter/fml/synchronization/waitable_event.h"
#include "flutter/fml/thread.h"
#include "flutter/runtime/dart_isolate.h"
#include "flutter/runtime/dart_vm.h"
#include "flutter/runtime/dart_vm_lifecycle.h"
#include "flutter/testing/dart_isolate_runner.h"
#include "flutter/testing/fixture_test.h"
#include "flutter/testing/testing.h"
#include "third_party/tonic/converter/dart_converter.h"
#include "third_party/tonic/scopes/dart_isolate_scope.h"

namespace flutter {
namespace testing {

using DartIsolateTest = FixtureTest;

TEST_F(DartIsolateTest, RootIsolateCreationAndShutdown) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  ASSERT_TRUE(vm_ref);
  auto vm_data = vm_ref.GetVMData();
  ASSERT_TRUE(vm_data);
  TaskRunners task_runners(GetCurrentTestName(),    //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner()   //
  );
  auto weak_isolate = DartIsolate::CreateRootIsolate(
      vm_data->GetSettings(),             // settings
      vm_data->GetIsolateSnapshot(),      // isolate snapshot
      std::move(task_runners),            // task runners
      nullptr,                            // window
      {},                                 // snapshot delegate
      {},                                 // io manager
      {},                                 // unref queue
      {},                                 // image decoder
      "main.dart",                        // advisory uri
      "main",                             // advisory entrypoint,
      nullptr,                            // flags
      settings.isolate_create_callback,   // isolate create callback
      settings.isolate_shutdown_callback  // isolate shutdown callback
  );
  auto root_isolate = weak_isolate.lock();
  ASSERT_TRUE(root_isolate);
  ASSERT_EQ(root_isolate->GetPhase(), DartIsolate::Phase::LibrariesSetup);
  ASSERT_TRUE(root_isolate->Shutdown());
}

TEST_F(DartIsolateTest, IsolateShutdownCallbackIsInIsolateScope) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  ASSERT_TRUE(vm_ref);
  auto vm_data = vm_ref.GetVMData();
  ASSERT_TRUE(vm_data);
  TaskRunners task_runners(GetCurrentTestName(),    //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner()   //
  );
  auto weak_isolate = DartIsolate::CreateRootIsolate(
      vm_data->GetSettings(),             // settings
      vm_data->GetIsolateSnapshot(),      // isolate snapshot
      std::move(task_runners),            // task runners
      nullptr,                            // window
      {},                                 // snapshot delegate
      {},                                 // io manager
      {},                                 // unref queue
      {},                                 // image decoder
      "main.dart",                        // advisory uri
      "main",                             // advisory entrypoint
      nullptr,                            // flags
      settings.isolate_create_callback,   // isolate create callback
      settings.isolate_shutdown_callback  // isolate shutdown callback
  );
  auto root_isolate = weak_isolate.lock();
  ASSERT_TRUE(root_isolate);
  ASSERT_EQ(root_isolate->GetPhase(), DartIsolate::Phase::LibrariesSetup);
  size_t destruction_callback_count = 0;
  root_isolate->AddIsolateShutdownCallback([&destruction_callback_count]() {
    ASSERT_NE(Dart_CurrentIsolate(), nullptr);
    destruction_callback_count++;
  });
  ASSERT_TRUE(root_isolate->Shutdown());
  ASSERT_EQ(destruction_callback_count, 1u);
}

TEST_F(DartIsolateTest, IsolateCanLoadAndRunDartCode) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  const auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  TaskRunners task_runners(GetCurrentTestName(),    //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner()   //
  );
  auto isolate = RunDartCodeInIsolate(vm_ref, settings, task_runners, "main",
                                      {}, GetFixturesPath());
  ASSERT_TRUE(isolate);
  ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);
}

TEST_F(DartIsolateTest, IsolateCannotLoadAndRunUnknownDartEntrypoint) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  const auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  TaskRunners task_runners(GetCurrentTestName(),    //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner()   //
  );
  auto isolate =
      RunDartCodeInIsolate(vm_ref, settings, task_runners, "thisShouldNotExist",
                           {}, GetFixturesPath());
  ASSERT_FALSE(isolate);
}

TEST_F(DartIsolateTest, CanRunDartCodeCodeSynchronously) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  const auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  TaskRunners task_runners(GetCurrentTestName(),    //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner(),  //
                           GetCurrentTaskRunner()   //
  );
  auto isolate = RunDartCodeInIsolate(vm_ref, settings, task_runners, "main",
                                      {}, GetFixturesPath());

  ASSERT_TRUE(isolate);
  ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);
  ASSERT_TRUE(isolate->RunInIsolateScope([]() -> bool {
    if (tonic::LogIfError(::Dart_Invoke(Dart_RootLibrary(),
                                        tonic::ToDart("sayHi"), 0, nullptr))) {
      return false;
    }
    return true;
  }));
}

TEST_F(DartIsolateTest, CanRegisterNativeCallback) {
  ASSERT_FALSE(DartVMRef::IsInstanceRunning());
  fml::AutoResetWaitableEvent latch;
  AddNativeCallback("NotifyNative",
                    CREATE_NATIVE_ENTRY(([&latch](Dart_NativeArguments args) {
                      FML_LOG(ERROR) << "Hello from Dart!";
                      latch.Signal();
                    })));
  const auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  auto thread = CreateNewThread();
  TaskRunners task_runners(GetCurrentTestName(),  //
                           thread,                //
                           thread,                //
                           thread,                //
                           thread                 //
  );
  auto isolate =
      RunDartCodeInIsolate(vm_ref, settings, task_runners,
                           "canRegisterNativeCallback", {}, GetFixturesPath());
  ASSERT_TRUE(isolate);
  ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);
  latch.Wait();
}

TEST_F(DartIsolateTest, CanSaveCompilationTrace) {
  if (DartVM::IsRunningPrecompiledCode()) {
    // Can only save compilation traces in JIT modes.
    GTEST_SKIP();
    return;
  }
  fml::AutoResetWaitableEvent latch;
  AddNativeCallback("NotifyNative",
                    CREATE_NATIVE_ENTRY(([&latch](Dart_NativeArguments args) {
                      ASSERT_TRUE(tonic::DartConverter<bool>::FromDart(
                          Dart_GetNativeArgument(args, 0)));
                      latch.Signal();
                    })));

  const auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  auto thread = CreateNewThread();
  TaskRunners task_runners(GetCurrentTestName(),  //
                           thread,                //
                           thread,                //
                           thread,                //
                           thread                 //
  );
  auto isolate = RunDartCodeInIsolate(vm_ref, settings, task_runners,
                                      "testCanSaveCompilationTrace", {},
                                      GetFixturesPath());
  ASSERT_TRUE(isolate);
  ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);

  latch.Wait();
}

TEST_F(DartIsolateTest, CanLaunchSecondaryIsolates) {
  fml::CountDownLatch latch(3);
  fml::AutoResetWaitableEvent child_shutdown_latch;
  fml::AutoResetWaitableEvent root_isolate_shutdown_latch;
  AddNativeCallback("NotifyNative",
                    CREATE_NATIVE_ENTRY(([&latch](Dart_NativeArguments args) {
                      latch.CountDown();
                    })));
  AddNativeCallback(
      "PassMessage", CREATE_NATIVE_ENTRY(([&latch](Dart_NativeArguments args) {
        auto message = tonic::DartConverter<std::string>::FromDart(
            Dart_GetNativeArgument(args, 0));
        ASSERT_EQ("Hello from code is secondary isolate.", message);
        latch.CountDown();
      })));
  auto settings = CreateSettingsForFixture();
  settings.root_isolate_shutdown_callback = [&root_isolate_shutdown_latch]() {
    root_isolate_shutdown_latch.Signal();
  };
  settings.isolate_shutdown_callback = [&child_shutdown_latch]() {
    child_shutdown_latch.Signal();
  };
  auto vm_ref = DartVMRef::Create(settings);
  auto thread = CreateNewThread();
  TaskRunners task_runners(GetCurrentTestName(),  //
                           thread,                //
                           thread,                //
                           thread,                //
                           thread                 //
  );
  auto isolate = RunDartCodeInIsolate(vm_ref, settings, task_runners,
                                      "testCanLaunchSecondaryIsolate", {},
                                      GetFixturesPath());
  ASSERT_TRUE(isolate);
  ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);
  child_shutdown_latch.Wait();  // wait for child isolate to shutdown first
  ASSERT_FALSE(root_isolate_shutdown_latch.IsSignaledForTest());
  latch.Wait();  // wait for last NotifyNative called by main isolate
  // root isolate will be auto-shutdown
}

TEST_F(DartIsolateTest, CanRecieveArguments) {
  fml::AutoResetWaitableEvent latch;
  AddNativeCallback("NotifyNative",
                    CREATE_NATIVE_ENTRY(([&latch](Dart_NativeArguments args) {
                      ASSERT_TRUE(tonic::DartConverter<bool>::FromDart(
                          Dart_GetNativeArgument(args, 0)));
                      latch.Signal();
                    })));

  const auto settings = CreateSettingsForFixture();
  auto vm_ref = DartVMRef::Create(settings);
  auto thread = CreateNewThread();
  TaskRunners task_runners(GetCurrentTestName(),  //
                           thread,                //
                           thread,                //
                           thread,                //
                           thread                 //
  );
  auto isolate = RunDartCodeInIsolate(vm_ref, settings, task_runners,
                                      "testCanRecieveArguments", {"arg1"},
                                      GetFixturesPath());
  ASSERT_TRUE(isolate);
  ASSERT_EQ(isolate->get()->GetPhase(), DartIsolate::Phase::Running);

  latch.Wait();
}

}  // namespace testing
}  // namespace flutter
