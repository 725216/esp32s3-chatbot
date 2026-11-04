set(AV_PROCESSOR_SRCS "src/av_pipeline_pool.c")

if(CONFIG_VIDEO_PROCESSOR_ENABLE)
    list(APPEND AV_PROCESSOR_SRCS "src/video_processor.c")
endif()

if(CONFIG_MEDIA_DUMP_ENABLE)
    list(APPEND AV_PROCESSOR_SRCS "src/media_dump.c")
endif()

if(CONFIG_AUDIO_PROCESSOR_IMPL_LIGHT)
    list(APPEND AV_PROCESSOR_SRCS "src/audio_processor_light.c")
else()
    list(APPEND AV_PROCESSOR_SRCS "src/audio_processor.c")
endif()

set(av_processor_requires esp_driver_gpio)

if(CONFIG_AUDIO_PROCESSOR_IMPL_GMF)
    list(APPEND av_processor_requires media_lib_sal)
endif()

if(CONFIG_VIDEO_PROCESSOR_ENABLE)
    list(APPEND av_processor_requires esp_capture esp_video_codec)
endif()

idf_component_register(
    SRCS ${AV_PROCESSOR_SRCS}
    INCLUDE_DIRS "include" "./"
    PRIV_INCLUDE_DIRS "private_include"
    PRIV_REQUIRES ${av_processor_requires}
)
