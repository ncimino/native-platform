import jetbrains.buildServer.configs.kotlin.v2019_2.BuildFeatures
import jetbrains.buildServer.configs.kotlin.v2019_2.BuildType
import jetbrains.buildServer.configs.kotlin.v2019_2.DslContext
import jetbrains.buildServer.configs.kotlin.v2019_2.Requirements
import jetbrains.buildServer.configs.kotlin.v2019_2.buildFeatures.commitStatusPublisher
import jetbrains.buildServer.configs.kotlin.v2019_2.buildFeatures.freeDiskSpace

/*
 * Copyright 2020 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

fun Requirements.requireAgent(agent: Agent) {
    agent.os.addAgentRequirements(this)
    contains("teamcity.agent.jvm.os.arch", agent.architecture.agentRequirementForOs(agent.os))
}

fun BuildType.runOn(agent: Agent) {
    params {
        param("env.JAVA_HOME", agent.java8Home)
    }

    requirements {
        requireAgent(agent)
    }
}

const val buildReceipt = "build-receipt.properties"

const val archiveReports = """build/reports/** => reports
buildSrc/build/reports/** => buildSrc/reports
testApp/build/reports/** => testApp/reports"""

fun BuildFeatures.publishCommitStatus() {
    commitStatusPublisher {
        vcsRootExtId = DslContext.settingsRoot.id?.value
        publisher = github {
            githubUrl = "https://api.github.com"
            authType = personalToken {
                token = "%github.bot-teamcity.token%"
            }
        }
    }
}

fun BuildFeatures.lowerRequiredFreeDiskSpace() {
    freeDiskSpace {
        // Configure less than the default 3GB, since the disk of the agents is only 5GB big.
        requiredSpace = "1gb"
        failBuild = false
    }
}
