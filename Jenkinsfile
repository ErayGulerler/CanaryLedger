pipeline {
    agent { label 'canary-linux' }
    options {
        skipDefaultCheckout(true)
        disableConcurrentBuilds()
        timeout(time: 30, unit: 'MINUTES')
        buildDiscarder(logRotator(numToKeepStr: '20'))
    }
    parameters {
        choice(name: 'ROLLOUT', choices: ['healthy', 'regression', 'incomplete', 'invalid'],
               description: 'Synthetic observation used for the release decision')
    }
    stages {
        stage('Checkout') {
            steps {
                deleteDir()
                checkout scm
                sh 'mkdir -p reports'
            }
        }
        stage('Build and test') {
            parallel {
                stage('CMake and CTest') {
                    steps {
                        sh '''
                            cmake --preset release
                            cmake --build --preset release
                            ctest --preset release --output-junit "$PWD/reports/ctest.xml"
                        '''
                    }
                    post { always { junit allowEmptyResults: true, testResults: 'reports/ctest.xml' } }
                }
                stage('Bazel') {
                    steps {
                        sh '''
                            bazel build //:canaryledger
                            bazel test //:gate_test
                        '''
                    }
                    post {
                        always {
                            sh 'if [ -f bazel-testlogs/gate_test/test.xml ]; then cp bazel-testlogs/gate_test/test.xml reports/bazel.xml; fi'
                            junit allowEmptyResults: true, testResults: 'reports/bazel.xml'
                        }
                    }
                }
            }
        }
        stage('Replay and compare builds') {
            steps {
                sh 'python3 tools/replay.py build/release/canaryledger --compare bazel-bin/canaryledger'
            }
        }
        stage('Evaluate rollout') {
            steps {
                script {
                    int result = sh(returnStatus: true, script: 'build/release/canaryledger policy/release.conf "examples/${ROLLOUT}.csv" > reports/decision.json')
                    if (result == 10) {
                        error('HOLD: collect more matched telemetry before packaging.')
                    } else if (result == 20) {
                        error('ROLLBACK: candidate exceeded rollout policy.')
                    } else if (result != 0) {
                        error("Invalid input or execution failure (exit ${result}).")
                    }
                }
            }
        }
        stage('Package approved CLI') {
            steps {
                sh '''
                    cmake --install build/release --prefix dist
                    git rev-parse HEAD > dist/COMMIT
                    cp reports/decision.json dist/
                    (cd dist && sha256sum bin/canaryledger share/canaryledger/release.conf COMMIT decision.json > SHA256SUMS)
                    tar -czf canaryledger-linux.tar.gz -C dist .
                '''
                archiveArtifacts artifacts: 'canaryledger-linux.tar.gz', fingerprint: true
            }
        }
    }
    post { always { archiveArtifacts artifacts: 'reports/**/*', allowEmptyArchive: true } }
}
