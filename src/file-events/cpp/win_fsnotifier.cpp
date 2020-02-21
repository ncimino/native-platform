#ifdef _WIN32

#include "win_fsnotifier.h"

using namespace std;

//
// WatchPoint
//

WatchPoint::WatchPoint(Server* server, const u16string& path, HANDLE directoryHandle) {
    this->server = server;
    this->path = path;
    this->buffer = (FILE_NOTIFY_INFORMATION*) malloc(EVENT_BUFFER_SIZE);
    ZeroMemory(&this->overlapped, sizeof(OVERLAPPED));
    this->overlapped.hEvent = this;
    this->directoryHandle = directoryHandle;
    this->status = WATCH_UNINITIALIZED;
}

WatchPoint::~WatchPoint() {
    free(buffer);
}

void WatchPoint::close() {
    BOOL ret = CancelIo(directoryHandle);
    if (!ret) {
        log_severe(server->getThreadEnv(), "Couldn't cancel I/O %p for '%ls': %d", directoryHandle, path.c_str(), GetLastError());
    }
    ret = CloseHandle(directoryHandle);
    if (!ret) {
        log_severe(server->getThreadEnv(), "Couldn't close handle %p for '%ls': %d", directoryHandle, path.c_str(), GetLastError());
    }
}

static void CALLBACK startWatchCallback(_In_ ULONG_PTR arg) {
    WatchPoint* watchPoint = (WatchPoint*) arg;
    watchPoint->listen();
}

int WatchPoint::awaitListeningStarted(HANDLE threadHandle) {
    unique_lock<mutex> lock(listenerMutex);
    QueueUserAPC(startWatchCallback, threadHandle, (ULONG_PTR) this);
    listenerStarted.wait(lock);
    return status;
}

static void CALLBACK handleEventCallback(DWORD errorCode, DWORD bytesTransferred, LPOVERLAPPED overlapped) {
    WatchPoint* watchPoint = (WatchPoint*) overlapped->hEvent;
    watchPoint->handleEvent(errorCode, bytesTransferred);
}

void WatchPoint::listen() {
    BOOL success = ReadDirectoryChangesW(
        directoryHandle,        // handle to directory
        buffer,                 // read results buffer
        EVENT_BUFFER_SIZE,      // length of buffer
        TRUE,                   // include children
        EVENT_MASK,             // filter conditions
        NULL,                   // bytes returned
        &overlapped,            // overlapped buffer
        &handleEventCallback    // completion routine
    );

    unique_lock<mutex> lock(listenerMutex);
    if (success) {
        status = WATCH_LISTENING;
    } else {
        status = WATCH_FAILED_TO_LISTEN;
        log_warning(server->getThreadEnv(), "Couldn't start watching %p for '%ls', error = %d", directoryHandle, path.c_str(), GetLastError());
        // TODO Error handling
    }
    listenerStarted.notify_all();
}

void WatchPoint::handleEvent(DWORD errorCode, DWORD bytesTransferred) {
    status = WATCH_NOT_LISTENING;

    if (errorCode == ERROR_OPERATION_ABORTED) {
        log_info(server->getThreadEnv(), "Finished watching '%ls'", path.c_str());
        status = WATCH_FINISHED;
        server->reportFinished(this);
        return;
    }

    if (bytesTransferred == 0) {
        // don't send dirty too much, everything is changed anyway
        // TODO Understand what this does
        // if (WaitForSingleObject(stopEventHandle, 500) == WAIT_OBJECT_0)
        //    break;

        // Got a buffer overflow => current changes lost => send INVALIDATE on root
        server->reportEvent(FILE_EVENT_INVALIDATE, path);
    } else {
        FILE_NOTIFY_INFORMATION* current = buffer;
        for (;;) {
            handlePathChanged(current);
            if (current->NextEntryOffset == 0) {
                break;
            }
            current = (FILE_NOTIFY_INFORMATION*) (((BYTE*) current) + current->NextEntryOffset);
        }
    }

    listen();
    if (status != WATCH_LISTENING) {
        server->reportFinished(this);
    }
}

void WatchPoint::handlePathChanged(FILE_NOTIFY_INFORMATION* info) {
    wstring changedPathW = wstring(info->FileName, 0, info->FileNameLength / sizeof(wchar_t));
    u16string changedPath(changedPathW.begin(), changedPathW.end());
    // TODO Do we ever get an empty path?
    if (!changedPath.empty()) {
        changedPath.insert(0, 1, u'\\');
        changedPath.insert(0, path);
    }
    // TODO Remove long prefix for path once?
    if (changedPath.length() >= 4 && changedPath.substr(0, 4) == u"\\\\?\\") {
        if (changedPath.length() >= 8 && changedPath.substr(0, 8) == u"\\\\?\\UNC\\") {
            changedPath.erase(0, 8).insert(0, u"\\\\");
        } else {
            changedPath.erase(0, 4);
        }
    }

    log_fine(server->getThreadEnv(), "Change detected: 0x%x '%ls'", info->Action, changedPathW.c_str());

    jint type;
    if (info->Action == FILE_ACTION_ADDED || info->Action == FILE_ACTION_RENAMED_NEW_NAME) {
        type = FILE_EVENT_CREATED;
    } else if (info->Action == FILE_ACTION_REMOVED || info->Action == FILE_ACTION_RENAMED_OLD_NAME) {
        type = FILE_EVENT_REMOVED;
    } else if (info->Action == FILE_ACTION_MODIFIED) {
        type = FILE_EVENT_MODIFIED;
    } else {
        log_warning(server->getThreadEnv(), "Unknown event 0x%x for %ls", info->Action, changedPathW.c_str());
        type = FILE_EVENT_UNKNOWN;
    }

    server->reportEvent(type, changedPath);
}

//
// Server
//

Server::Server(JNIEnv* env, jobject watcherCallback)
    : AbstractServer(env, watcherCallback) {
    startThread();
    // TODO Error handling
    SetThreadPriority(this->watcherThread.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
}

Server::~Server() {
}

void Server::runLoop(JNIEnv* env, function<void(exception_ptr)> notifyStarted) {
    notifyStarted(nullptr);

    while (!terminate || watchPoints.size() > 0) {
        SleepEx(INFINITE, true);
    }
}

void Server::startWatching(JNIEnv* env, const u16string& path) {
    wstring pathW(path.begin(), path.end());
    HANDLE directoryHandle = CreateFileW(
        pathW.c_str(),          // pointer to the file name
        FILE_LIST_DIRECTORY,    // access (read/write) mode
        CREATE_SHARE,           // share mode
        NULL,                   // security descriptor
        OPEN_EXISTING,          // how to create
        CREATE_FLAGS,           // file attributes
        NULL                    // file with attributes to copy
    );

    if (directoryHandle == INVALID_HANDLE_VALUE) {
        log_severe(env, "Couldn't get file handle for '%ls': %d", pathW.c_str(), GetLastError());
        // TODO Error handling
        return;
    }

    WatchPoint* watchPoint = new WatchPoint(this, path, directoryHandle);

    HANDLE threadHandle = watcherThread.native_handle();
    int ret = watchPoint->awaitListeningStarted(threadHandle);
    switch (ret) {
        case WATCH_LISTENING:
            watchPoints.push_back(watchPoint);
            break;
        default:
            log_severe(env, "Couldn't start listening to '%ls': %d", pathW.c_str(), ret);
            delete watchPoint;
            // TODO Error handling
            break;
    }
}

void Server::reportFinished(WatchPoint* watchPoint) {
    watchPoints.remove(watchPoint);
    delete watchPoint;
}

void Server::reportEvent(jint type, const u16string& changedPath) {
    JNIEnv* env = getThreadEnv();
    reportChange(env, type, changedPath);
}

static void CALLBACK requestTerminationCallback(_In_ ULONG_PTR arg) {
    Server* server = (Server*) arg;
    server->requestTermination();
}

void Server::requestTermination() {
    terminate = true;
    // Make copy so terminated entries can be removed
    list<WatchPoint*> copyWatchPoints(watchPoints);
    for (auto& watchPoint : copyWatchPoints) {
        watchPoint->close();
    }
}

void Server::close(JNIEnv* env) {
    HANDLE threadHandle = watcherThread.native_handle();
    log_fine(env, "Requesting termination of server thread %p", threadHandle);
    int ret = QueueUserAPC(requestTerminationCallback, threadHandle, (ULONG_PTR) this);
    if (ret == 0) {
        log_severe(env, "Couldn't send termination request to thread %p: %d", threadHandle, GetLastError());
    } else {
        watcherThread.join();
    }
}

bool isAbsoluteLocalPath(const u16string& path) {
    if (path.length() < 3) {
        return false;
    }
    return ((u'a' <= path[0] && path[0] <= u'z') || (u'A' <= path[0] && path[0] <= u'Z'))
        && path[1] == u':'
        && path[2] == u'\\';
}

bool isAbsoluteUncPath(const u16string& path) {
    if (path.length() < 3) {
        return false;
    }
    return path[0] == u'\\' && path[1] == u'\\';
}

void convertToLongPathIfNeeded(u16string& path) {
    // Technically, this should be MAX_PATH (i.e. 260), except some Win32 API related
    // to working with directory paths are actually limited to 240. It is just
    // safer/simpler to cover both cases in one code path.
    if (path.length() <= 240) {
        return;
    }

    if (isAbsoluteLocalPath(path)) {
        // Format: C:\... -> \\?\C:\...
        path.insert(0, u"\\\\?\\");
    } else if (isAbsoluteUncPath(path)) {
        // In this case, we need to skip the first 2 characters:
        // Format: \\server\share\... -> \\?\UNC\server\share\...
        path.erase(0, 2);
        path.insert(0, u"\\\\?\\UNC\\");
    } else {
        // It is some sort of unknown format, don't mess with it
    }
}

//
// JNI calls
//

JNIEXPORT jobject JNICALL
Java_net_rubygrapefruit_platform_internal_jni_WindowsFileEventFunctions_startWatching(JNIEnv* env, jclass target, jobjectArray paths, jobject javaCallback) {
    Server* server = new Server(env, javaCallback);

    int watchPointCount = env->GetArrayLength(paths);
    for (int i = 0; i < watchPointCount; i++) {
        jstring javaPath = (jstring) env->GetObjectArrayElement(paths, i);
        jsize javaPathLength = env->GetStringLength(javaPath);
        const jchar* javaPathChars = env->GetStringCritical(javaPath, nullptr);
        if (javaPathChars == NULL) {
            // TODO Throw Java exception
            fprintf(stderr, "Could not get Java string character");
            return NULL;
        }
        u16string pathStr((char16_t*) javaPathChars, javaPathLength);
        env->ReleaseStringCritical(javaPath, javaPathChars);
        convertToLongPathIfNeeded(pathStr);
        server->startWatching(env, pathStr);
    }

    jclass clsWatch = env->FindClass("net/rubygrapefruit/platform/internal/jni/WindowsFileEventFunctions$WatcherImpl");
    jmethodID constructor = env->GetMethodID(clsWatch, "<init>", "(Ljava/lang/Object;)V");
    return env->NewObject(clsWatch, constructor, env->NewDirectByteBuffer(server, sizeof(server)));
}

JNIEXPORT void JNICALL
Java_net_rubygrapefruit_platform_internal_jni_WindowsFileEventFunctions_stopWatching(JNIEnv* env, jclass target, jobject detailsObj) {
    Server* server = (Server*) env->GetDirectBufferAddress(detailsObj);
    server->close(env);
    delete server;
}

#endif
