/**
 * Copyright 2023 Google LLC
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

locals {
  region      = "" # Example: ["us-central1", "us-west1"]
  environment = "" # Must be <= 3 characters. Example: "abc"
}
provider "aws" {
  region = local.region
}


module "seller" {
  source                               = "../../../modules/seller"
  environment                          = local.environment
  region                               = local.region
  enclave_debug_mode                   = false # Example: false
  root_domain                          = ""    # Example: "seller-frontend.com"
  root_domain_zone_id                  = ""    # Example: "Z010286721PKVM00UYU50"
  certificate_arn                      = ""    # Example: "arn:aws:acm:us-west-1:574738241422:certificate/b8b0a33f-0821-1111-8464-bc8da130a7fe"
  operator                             = ""    # Example: "seller1"
  sfe_instance_ami_id                  = ""    # Example: "ami-0ff8ad2fa8512a078"
  auction_instance_ami_id              = ""    # Example: "ami-0ea85f493f16aba3c"
  sfe_instance_type                    = ""    # Example: "c6i.2xlarge"
  auction_instance_type                = ""    # Example: "c6i.2xlarge"
  sfe_enclave_cpu_count                = 6     # Example: 6
  sfe_enclave_memory_mib               = 12000 # Example: 12000
  auction_enclave_cpu_count            = 6     # Example: 6
  auction_enclave_memory_mib           = 12000 # Example: 12000
  sfe_autoscaling_desired_capacity     = 3     # Example: 3
  sfe_autoscaling_max_size             = 5     # Example: 5
  sfe_autoscaling_min_size             = 1     # Example: 1
  auction_autoscaling_desired_capacity = 3     # Example: 3
  auction_autoscaling_max_size         = 5     # Example: 5
  auction_autoscaling_min_size         = 1     # Example: 1

  runtime_flags = {
    AUCTION_PORT         = "50051" # Do not change unless you are modifying the default GCP architecture.
    SELLER_FRONTEND_PORT = "50051" # Do not change unless you are modifying the default GCP architecture.
    SFE_INGRESS_TLS      = "false" # Do not change unless you are modifying the default GCP architecture.
    BUYER_EGRESS_TLS     = "true"  # Do not change unless you are modifying the default GCP architecture.
    AUCTION_EGRESS_TLS   = "true"  # Do not change unless you are modifying the default GCP architecture.

    ENABLE_AUCTION_SERVICE_BENCHMARK       = "" # Example: "false"
    GET_BID_RPC_TIMEOUT_MS                 = "" # Example: "60000"
    KEY_VALUE_SIGNALS_FETCH_RPC_TIMEOUT_MS = "" # Example: "60000"
    SCORE_ADS_RPC_TIMEOUT_MS               = "" # Example: "60000"
    SELLER_ORIGIN_DOMAIN                   = "" # Example: "https://securepubads.g.doubleclick.net"
    AUCTION_SERVER_HOST                    = "" # Example: "dns:///auction.seller-frontend.com:443"
    KEY_VALUE_SIGNALS_HOST                 = "" # Example: "https://pubads.g.doubleclick.net/td/sts"
    BUYER_SERVER_HOSTS                     = "" # Example: "{\"https://bid1.com\": \"dns:///bidding1.com:443\"}"
    ENABLE_SELLER_FRONTEND_BENCHMARKING    = "" # Example: "false"
    ENABLE_AUCTION_COMPRESSION             = "" # Example: "false"
    ENABLE_BUYER_COMPRESSION               = "" # Example: "false"
    CREATE_NEW_EVENT_ENGINE                = "" # Example: "false"
    ENABLE_ENCRYPTION                      = "" # Example: "true"
    TELEMETRY_CONFIG                       = "" # Example: "mode: EXPERIMENT"
    TEST_MODE                              = "" # Example: "false"
    SELLER_CODE_FETCH_CONFIG               = "" # Example:
    # "{
    #     "auctionJsPath": "",
    #     "auctionJsUrl": "https://example.com/scoreAd.js",
    #     "urlFetchPeriodMs": 13000000,
    #     "urlFetchTimeoutMs": 30000,
    #     "enableSellerDebugUrlGeneration": true,
    #     "enableAdtechCodeLogging": false,
    #     "enableReportResultUrlGeneration": false,
    #     "enableReportWinUrlGeneration": false,
    #     "buyerReportWinJsUrls": {"https://buyerA_origin.com":"https://buyerA.com/generateBid.js",
    #                              "https://buyerB_origin.com":"https://buyerB.com/generateBid.js",
    #                              "https://buyerC_origin.com":"https://buyerC.com/generateBid.js"}
    #  }"
    JS_NUM_WORKERS      = "" # Example: "48" Must be <=vCPUs in auction_enclave_cpu_count.
    JS_WORKER_QUEUE_LEN = "" # Example: "100".
    JS_WORKER_MEM_MB    = "" # Example: "1536" JS_WORKER_MEM_MB/JS_WORKER_QUEUE_LEN > average JS request size.
    ROMA_TIMEOUT_MS     = "" # Example: "10000"
    # This flag should only be set if console.logs from the AdTech code(Ex:scoreAd(), reportResult(), reportWin())
    # execution need to be exported as VLOG.
    # Note: turning on this flag will lead to higher memory consumption for AdTech code execution
    # and additional latency for parsing the logs.

    # This flag should only be set if console.logs from the AdTech code(Ex:scoreAd(), reportResult(), reportWin())
    # execution need to be exported as VLOG.
    # Note: turning on this flag will lead to higher memory consumption for AdTech code execution
    # and additional latency for parsing the logs.
    ENABLE_ADTECH_CODE_LOGGING = "" # Example: "false"
    ENABLE_SELLER_CODE_WRAPPER = "" # Example: "true"

    # Reach out to the Privacy Sandbox B&A team to enroll with Coordinators and update the following flag values.
    # More information on enrollment can be found here: https://github.com/privacysandbox/fledge-docs/blob/main/bidding_auction_services_api.md#enroll-with-coordinators
    # Coordinator-based attestation flags:
    PUBLIC_KEY_ENDPOINT                        = "" # Example: "https://test.cloudfront.net/v1alpha/publicKeys"
    PRIMARY_COORDINATOR_PRIVATE_KEY_ENDPOINT   = "" # Example: "https://test.execute-api.us-east-1.amazonaws.com/stage/v1alpha/encryptionKeys"
    SECONDARY_COORDINATOR_PRIVATE_KEY_ENDPOINT = "" # Example: "https://test.execute-api.us-east-1.amazonaws.com/stage/v1alpha/encryptionKeys"
    PRIMARY_COORDINATOR_ACCOUNT_IDENTITY       = "" # Example: "arn:aws:iam::574738241422:role/mp-prim-ba_574738241422_coordinator_assume_role"
    SECONDARY_COORDINATOR_ACCOUNT_IDENTITY     = "" # Example: "arn:aws:iam::574738241422:role/mp-sec-ba_574738241422_coordinator_assume_role"
    PRIMARY_COORDINATOR_REGION                 = "" # Example: "us-east-1"
    SECONDARY_COORDINATOR_REGION               = "" # Example: "us-east-1"
    PRIVATE_KEY_CACHE_TTL_SECONDS              = "" # Example: "3974400" (46 days)
    KEY_REFRESH_FLOW_RUN_FREQUENCY_SECONDS     = "" # Example: "10800"
  }
}
