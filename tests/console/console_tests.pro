include(../common/test_defaults.pri)

QT += core network

TEMPLATE = app
TARGET = console_tests

SOURCES += \
    $$REPO_ROOT/tests/console/test_fpm_name_validator.cpp \
    $$REPO_ROOT/tests/console/test_atomic_file_replace.cpp \
    $$REPO_ROOT/tests/common/test_artifacts.cpp \
    $$REPO_ROOT/tests/common/frame_compare.cpp \
    $$REPO_ROOT/tests/common/hash_helpers.cpp \
    $$REPO_ROOT/tests/common/repo_paths.cpp \
    $$REPO_ROOT/platform/qt/ReceiptSettings.cpp \
    $$REPO_ROOT/platform/qt/DownloadManager.cpp \
    $$REPO_ROOT/src/mlv/frame_caching.c \
    $$REPO_ROOT/src/mlv/pipeline_stage_capture.c \
    $$REPO_ROOT/src/batch/BatchContext.cpp \
    $$REPO_ROOT/src/batch/BatchRenderedVideoPlan.cpp \
    $$REPO_ROOT/src/batch/BatchLogger.cpp \
    $$REPO_ROOT/src/batch/ReceiptLoader.cpp \
    $$REPO_ROOT/src/batch/ReceiptApplier.cpp \
    $$REPO_ROOT/tests/console/stubs/pipeline_stubs.cpp \
    $$REPO_ROOT/tests/console/test_main.cpp \
    $$REPO_ROOT/tests/console/test_clip_golden.cpp \
    $$REPO_ROOT/tests/console/test_cache_behavior.cpp \
    $$REPO_ROOT/tests/console/test_avx_golden.cpp \
    $$REPO_ROOT/tests/console/test_worker_thread_count.cpp \
    $$REPO_ROOT/tests/console/test_env_flags.cpp \
    $$REPO_ROOT/tests/console/test_dual_iso_playback_policy.cpp \
    $$REPO_ROOT/tests/console/test_dual_iso_level_sync_policy.cpp \
    $$REPO_ROOT/tests/console/test_frame_compare.cpp \
    $$REPO_ROOT/tests/console/test_phase3_quality_policy.cpp \
    $$REPO_ROOT/tests/console/test_clip_lifecycle_barrier.cpp \
    $$REPO_ROOT/tests/console/test_export_dimensions.cpp \
    $$REPO_ROOT/tests/console/test_export_process.cpp \
    $$REPO_ROOT/tests/console/test_playback_frame_range.cpp \
    $$REPO_ROOT/tests/console/test_playback_quality_settings.cpp \
    $$REPO_ROOT/tests/console/test_shipping_defaults.cpp \
    $$REPO_ROOT/tests/console/test_playback_quality_auto_mode.cpp \
    $$REPO_ROOT/tests/console/test_receipt_loader.cpp \
    $$REPO_ROOT/tests/console/test_receipt_applier.cpp \
    $$REPO_ROOT/tests/console/test_rendered_video_runner.cpp \
    $$REPO_ROOT/tests/console/test_sync_download_waiter.cpp \
    $$REPO_ROOT/tests/console/test_download_manager.cpp \
    $$REPO_ROOT/tests/console/test_playback_gate_policy.cpp

HEADERS += \
    $$REPO_ROOT/platform/qt/FpmNameValidator.h \
    $$REPO_ROOT/platform/qt/AtomicFileReplace.h \
    $$REPO_ROOT/platform/qt/SyncDownloadWaiter.h \
    $$REPO_ROOT/platform/qt/DownloadManager.h \
    $$REPO_ROOT/tests/common/minitest.h \
    $$REPO_ROOT/tests/common/test_artifacts.h \
    $$REPO_ROOT/tests/common/test_runtime.h \
    $$REPO_ROOT/tests/common/frame_compare.h \
    $$REPO_ROOT/tests/common/hash_helpers.h \
    $$REPO_ROOT/tests/common/repo_paths.h \
    $$REPO_ROOT/src/batch/BatchTypes.h \
    $$REPO_ROOT/src/batch/BatchRenderedVideoPlan.h \
    $$REPO_ROOT/src/batch/EnvFlags.h \
    $$REPO_ROOT/src/batch/BatchRunner.h \
    $$REPO_ROOT/src/batch/RawAspectStretchPolicy.h \
    $$REPO_ROOT/platform/qt/ClipLifecycleBarrier.h \
    $$REPO_ROOT/platform/qt/ExportDimensions.h \
    $$REPO_ROOT/platform/qt/ExportProcess.h \
    $$REPO_ROOT/platform/qt/DualIsoLevelSyncPolicy.h \
    $$REPO_ROOT/platform/qt/PlaybackFrameRange.h \
    $$REPO_ROOT/platform/qt/PlaybackPrepPresentationPolicy.h \
    $$REPO_ROOT/platform/qt/PlaybackGatePolicy.h

win32{
    WINDOWS_TEST_RUNTIME_DEPLOY = $$relative_path($$REPO_ROOT/tools/testing/deploy-windows-test-runtime.ps1, $$OUT_PWD)
    QMAKE_POST_LINK += powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $$WINDOWS_TEST_RUNTIME_DEPLOY -TargetDir release -QtBinDir $$[QT_INSTALL_BINS] -ExeName console_tests.exe $$escape_expand(\n\t)
}
