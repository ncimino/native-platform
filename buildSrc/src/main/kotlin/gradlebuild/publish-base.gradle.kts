package gradlebuild

plugins {
    base
    `maven-publish`
}

val bintrayCredentials = project.extensions.create<BintrayCredentials>("bintray")
val variants = project.extensions.create<VariantsExtension>("variants").apply {
    groupId.convention(provider { project.group.toString() })
    artifactId.convention(provider { getArchivesBaseName(project) })
    version.convention(provider { project.version.toString() })
}

if (project.hasProperty("bintrayUserName")) {
    bintrayCredentials.userName = providers.gradleProperty("bintrayUserName").forUseAtConfigurationTime().get()
}
if (project.hasProperty("bintrayApiKey")) {
    bintrayCredentials.apiKey = providers.gradleProperty("bintrayApiKey").forUseAtConfigurationTime().get()
}

val localRepoDir = Callable { rootProject.layout.buildDirectory.dir("repo").get().asFile }
val jarTask = tasks.named("jar", Jar::class.java)
val sourceZipTask = tasks.register<Jar>("sourceZip") { archiveClassifier.set("sources") }
val javadocZipTask = tasks.register<Jar>("javadocZip") { archiveClassifier.set("javadoc") }

publishing {
    repositories.maven {
        name = BasePublishPlugin.LOCAL_FILE_REPOSITORY_NAME
        setUrl(localRepoDir)
    }
    publications.create<MavenPublication>("main") {
        artifact(jarTask.get())
        artifact(sourceZipTask.get())
        artifact(javadocZipTask.get())
    }
}

project.afterEvaluate {
    publishing {
        publications.named<MavenPublication>("main") {
            groupId = project.group.toString()
            artifactId = jarTask.get().archiveBaseName.get()
            version = project.version.toString()
        }
    }
}

fun getArchivesBaseName(project: Project): String {
    val convention = project.convention.getPlugin(BasePluginConvention::class.java)
    return convention.archivesBaseName
}

