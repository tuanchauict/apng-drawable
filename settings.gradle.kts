rootProject.name = "apng-drawable-root"

pluginManagement {
    repositories {
        gradlePluginPortal()
        google()
    }
}

include(":apng-drawable")
include(":sample-app")
include(":sample-compose")

project(":apng-drawable").name = "apng-drawable"
project(":sample-app").name = "sample-app"
project(":sample-compose").name = "sample-compose"
