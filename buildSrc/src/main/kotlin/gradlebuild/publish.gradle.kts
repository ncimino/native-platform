package gradlebuild

plugins {
    id("gradlebuild.publish-base")
}

val bintrayCredentials = the<BintrayCredentials>()

publishing {
    repositories {
        VersionDetails.ReleaseRepository.values()
            .filter { it.type == VersionDetails.RepositoryType.Maven }
            .forEach { repository ->
                maven {
                    setUrl(repository.url)
                    name = repository.name
                    credentials {
                        username = bintrayCredentials.userName
                        password = bintrayCredentials.apiKey
                    }
                }
            }
    }
}
