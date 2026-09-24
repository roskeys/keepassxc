if(WITH_XC_REMOTESYNC)
    set(remotesync_SOURCES
        RemoteSyncSettings.cpp
        SyncProviderFactory.cpp
        providers/WebDavSyncProvider.cpp
        providers/DropboxSyncProvider.cpp
        providers/GoogleDriveSyncProvider.cpp
        providers/SftpSyncProvider.cpp
        providers/S3SyncProvider.cpp
        providers/GitSyncProvider.cpp
        RemoteSyncManager.cpp
        DatabaseSettingsWidgetRemoteSync.cpp
    )

    set(remotesync_HEADERS
        RemoteSyncSettings.h
        SyncResult.h
        ISyncProvider.h
        SyncProviderFactory.h
        providers/WebDavSyncProvider.h
        providers/DropboxSyncProvider.h
        providers/GoogleDriveSyncProvider.h
        providers/SftpSyncProvider.h
        providers/S3SyncProvider.h
        providers/GitSyncProvider.h
        RemoteSyncManager.h
        DatabaseSettingsWidgetRemoteSync.h
    )

    add_library(remotesync STATIC ${remotesync_SOURCES} ${remotesync_HEADERS} ${remotesync_UIS})
    target_link_libraries(remotesync PUBLIC Qt6::Core Qt6::Network Qt6::Widgets Qt6::Concurrent Qt6::Xml)
    target_include_directories(remotesync PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_BINARY_DIR})
endif()
