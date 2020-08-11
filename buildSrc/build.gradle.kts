plugins {
    `kotlin-dsl`
}

repositories {
    jcenter()
    gradlePluginPortal()
}

dependencies {
    implementation(gradleApi())
    implementation("org.apache.httpcomponents.client5:httpclient5:5.0.1")
    implementation("com.google.guava:guava:28.2-jre")
    implementation("org.gradle:test-retry-gradle-plugin:1.1.8")
}

java {
    sourceCompatibility = JavaVersion.VERSION_1_8
    targetCompatibility = JavaVersion.VERSION_1_8
}

if (JavaVersion.current() < JavaVersion.VERSION_1_8) {
    // Need to use Java 8 on some older platforms
    throw UnsupportedOperationException("At least Java 8 is required to build native-platform. Earlier versions are not supported.")
}


gradlePlugin {
    plugins {
        create("jni") {
            id = "gradlebuild.jni"
            implementationClass = "gradlebuild.JniPlugin"
        }
        create("nativeComponent") {
            id = "gradlebuild.native-platform-component"
            implementationClass = "gradlebuild.NativePlatformComponentPlugin"
        }
        create("freebsd") {
            id = "gradlebuild.freebsd"
            implementationClass = "gradlebuild.FreeBsdPlugin"
        }
        create("ncurses") {
            id = "gradlebuild.ncurses"
            implementationClass = "gradlebuild.NcursesPlugin"
        }
    }
}
