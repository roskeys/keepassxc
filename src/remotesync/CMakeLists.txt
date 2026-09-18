if(WITH_XC_REMOTESYNC)
    set(remotesync_SOURCES
        RemoteSyncSettings.cpp
        SyncProviderFactory.cpp
        providers/WebDavSyncProvider.cpp
        providers/SftpSyncProvider.cpp
        providers/S3SyncProvider.cpp
        RemoteSyncManager.cpp
        DatabaseSettingsWidgetRemoteSync.cpp
    )

    set(remotesync_HEADERS
        RemoteSyncSettings.h
        SyncResult.h
        ISyncProvider.h
        SyncProviderFactory.h
        providers/WebDavSyncProvider.h
        providers/SftpSyncProvider.h
        providers/S3SyncProvider.h
        RemoteSyncManager.h
        DatabaseSettingsWidgetRemoteSync.h
    )

    set(remotesync_UIS
        DatabaseSettingsWidgetRemoteSync.ui
    )

    qt5_wrap_ui(remotesync_UI_SOURCES ${remotesync_UIS})

    add_library(remotesync STATIC ${remotesync_SOURCES} ${remotesync_HEADERS} ${remotesync_UI_SOURCES})
    target_link_libraries(remotesync Qt5::Core Qt5::Network Qt5::Xml Qt5::Widgets Qt5::Concurrent)
    target_include_directories(remotesync PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_BINARY_DIR})
endif()
