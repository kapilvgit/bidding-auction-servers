# Running servers locally

The scripts in this folder can be used to run B&A servers in a local system. This can be used for
sanity testing B&A locally or for debugging B&A code in test_mode. The scripts use the local
binaries to start the servers on local ports without HTTPS. The logs will be output to
stderr/stdout.

## Usage

### Build with local bazel

#### Prerequisites

Install Python 3 Clang (if not installed).

```bash
sudo apt install python3-clang
```

#### Build Buyer stack

```bash
bazel build //services/bidding_service:server --config=local_local
bazel build //services/buyer_frontend_service:server --config=local_local
```

#### Start Buyer stack

```bash
# Edit the run time flags in the scripts:
# Eg. change the biddingJsUrl in tools/debug/start_bidding for custom generateBid script.
# Eg. change the buyer_kv_server_addr in tools/debug/start_bfe for custom KV server.
# Eg. change the GLOG_v value to increase/decrease log level.

# Open two new terminals at B&A project root.
# Start the Bidding server in terminal 1:
./tools/debug/start_bidding
# You should see some logs in each server as it displays HTTP metrics for the first call to the generateBid JS endpoint and some errors for OTEL collectors not found.

# Start the BuyerFrontEnd server in terminal 2:
./tools/debug/start_bfe
# You should see some logs in each server as it displays HTTP metrics for the first call to the KV server and some errors for OTEL collectors not found.
```

#### Build Seller stack

```bash
bazel build //services/auction_service:server --config=local_local
bazel build //services/seller_frontend_service:server --config=local_local
```

#### Start Seller stack

```bash
# Edit the run time flags in the scripts:
# Eg. change the auctionJsUrl in tools/debug/start_auction for custom scoreAd script.
# Eg. change the key_value_signals_host in tools/debug/start_sfe for custom KV server.
# Eg. change the GLOG_v value to increase/decrease log level.

# Open two new terminals at B&A project root.
# Start the Auction server in terminal 1:
./tools/debug/start_auction
# You should see some logs in each server as it displays HTTP metrics for the first call to the scoreAd JS endpoint and some errors for OTEL collectors not found.

# Start the SellerFrontEnd server in terminal 2:
./tools/debug/start_sfe
# You should see some logs in each server as it displays HTTP metrics for the first call to the KV server and some errors for OTEL collectors not found.
```

### Build with docker

#### Prerequisites

Docker daemon should be up and running.

#### Build Buyer stack

```bash
./production/packaging/build_and_test_all_in_docker --service-path bidding_service --service-path buyer_frontend_service --instance local --platform aws --build-flavor non_prod
```

#### Start Buyer stack

```bash
# Find Bazel Build Path
# The path for the server binaries will be different in case of docker builds. You can find the root
# of the build directory as follows -
# Run in bidding-auction-server root to find bazel build root
ls -al
# Should print out the following as part of the output
# bazel-bin -> /bazel_root/build_ubuntu_b5f37d9/eb7c660ef3781542ec00a071f7f762a5/execroot/__main__/bazel-out/k8-opt/bin

# Copy the directory name starting [build_ubuntu_b5f37d9/eb7c660ef3781542ec00a071f7f762a5]
# Find the root of bazel cache in your system. Should be ~/.cache/bazel/
# Final path is a combination of the two - ~/.cache/bazel/build_ubuntu_b5f37d9/eb7c660ef3781542ec00a071f7f762a5

# Edit the run time flags in the scripts:
# Eg. change the biddingJsUrl in tools/debug/start_bidding for custom generateBid script.
# Eg. change the buyer_kv_server_addr in tools/debug/start_bfe for custom KV server.
# Eg. change the GLOG_v value to increase/decrease log level.

# Open two new terminals at B&A project root.
# Start the Bidding server in terminal 1 with bazel build folder:
./tools/debug/start_bidding ~/.cache/bazel/build_ubuntu_b5f37d9/eb7c660ef3781542ec00a071f7f762a5
# You should see some logs in each server as it displays HTTP metrics for the first call to the generateBid JS endpoint and some errors for OTEL collectors not found.

# Start the BuyerFrontEnd server in terminal 2 with bazel build folder:
./tools/debug/start_bfe ~/.cache/bazel/build_ubuntu_b5f37d9/eb7c660ef3781542ec00a071f7f762a5
# You should see some logs in each server as it displays HTTP metrics for the first call to the KV server and some errors for OTEL collectors not found.
```

#### Build Seller stack

```bash
./production/packaging/build_and_test_all_in_docker --service-path seller_frontend_service --service-path auction_service --instance local --platform aws --build-flavor non_prod
```

#### Start Seller stack

```bash
# Find Bazel Build Path
# The path for the server binaries will be different in case of docker builds. You can find the root
# of the build directory as follows -
# Run in bidding-auction-server root to find bazel build root
ls -al
# Should print out the following as part of the output
# bazel-bin -> /bazel_root/build_ubuntu_b5f37d9/eb7c660ef3781542ec00a071f7f762a5/execroot/__main__/bazel-out/k8-opt/bin

# Copy the directory name starting [build_ubuntu_b5f37d9/eb7c660ef3781542ec00a071f7f762a5]
# Find the root of bazel cache in your system. Should be ~/.cache/bazel/
# Final path is a combination of the two - ~/.cache/bazel/build_ubuntu_b5f37d9/eb7c660ef3781542ec00a071f7f762a5

# Edit the run time flags in the scripts:
# Eg. change the auctionJsUrl in tools/debug/start_auction for custom scoreAd script.
# Eg. change the key_value_signals_host in tools/debug/start_sfe for custom KV server.
# Eg. change the GLOG_v value to increase/decrease log level.

# Open two new terminals at B&A project root.
# Start the Auction server in terminal 1 with bazel build folder:
./tools/debug/start_auction ~/.cache/bazel/build_ubuntu_b5f37d9/eb7c660ef3781542ec00a071f7f762a5
# You should see some logs in each server as it displays HTTP metrics for the first call to the scoreAd JS endpoint and some errors for OTEL collectors not found.

# Start the SellerFrontEnd server in terminal 2 with bazel build folder:
./tools/debug/start_sfe ~/.cache/bazel/build_ubuntu_b5f37d9/eb7c660ef3781542ec00a071f7f762a5
# You should see some logs in each server as it displays HTTP metrics for the first call to the KV server and some errors for OTEL collectors not found.
```

### Test Buyer stack

#### Plaintext request

Plaintext requests need to be created manually. For a Buyer stack, this should be a valid
GetBidsRawRequest object.

You can use the [secure_invoke] tool for sending a request to the buyer stack. The plaintext request
will be encrypted with a hardcoded key that the server can understand when running with the
test_mode flag set to true. Example -

```bash
# Setup arguments.
INPUT_PATH=/tmp/get_bids_request.txt  # Needs to be a valid GetBidsRawRequest
INPUT_FORMAT=PROTO
CLIENT_IP=<A valid client IPv4 address>

# Run the tool with desired arguments.
./builders/tools/bazel-debian run //tools/secure_invoke:invoke \
    -- \
    -target_service=bfe \
    -input_file="/src/workspace/${INPUT_PATH}" \
    -input_format=${INPUT_FORMAT} \
    -host_addr="localhost:50051" \
    -client_ip=${CLIENT_IP} \
    -insecure=true
```

#### Encrypted request

Encrypted request must be a valid GetBidsRequest with an encrypted request_ciphertext. Currently,
there is no recommended way to get an encrypted ciphertext for testing.

### Test Seller stack

#### Plaintext request

Plaintext requests need to be created manually. For the expected format for this request, please
refer to the [secure_invoke] section.

You can use the [secure_invoke] tool for sending a request to the buyer stack. The plaintext request
will be encrypted with a hardcoded key that the server can understand when running with the
test_mode flag set to true. Example -

```bash
# Setup arguments.
INPUT_PATH=select_ad_request.json  # Needs to be a valid plaintext request in the root of the B&A project (i.e. the path is .../bidding-auction-server/select_ad_request.json)
CLIENT_IP=<A valid client IPv4 address>

# Run the tool with desired arguments.
./builders/tools/bazel-debian run //tools/secure_invoke:invoke \
    -- \
    -target_service=sfe \
    -input_file="/src/workspace/${INPUT_PATH}" \
    -host_addr="localhost:50053" \
    -client_ip=${CLIENT_IP} \
    -insecure=true
```

#### Encrypted request

Encrypted requests must be valid SelectAdRequests with an encrypted protectedAudienceCiphertext. The
ciphertext can be obtained from the client side (Chrome/Android), and the auction config has to be
populated manually. There are two ways to send encrypted requests to local servers -

1. gRPC - You can use [grpcurl] to send an encrypted payload to the seller stack.

    ```bash
    grpcurl --plaintext -H 'X-BnA-Client-IP:<A valid client IPv4 address>' -H 'X-User-Agent:Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/111.0.0.0 Safari/537.36' -H 'x-accept-language: en-US,en;q=0.9' -d '@' localhost:50053 privacy_sandbox.bidding_auction_servers.SellerFrontEnd/SelectAd < select_ad_request.json
    ```

2. HTTP - To send HTTP requests, you will have to route the requests through [envoy]. Envoy should
   be started in a separate terminal as follows -

    ```bash
    PROJECT_PATH=<Path to B&A project eg. ~/projects>
    docker run --rm -t --network="host" -v $PROJECT_PATH/bidding-auction-server/bazel-bin/api/bidding_auction_servers_descriptor_set.pb:/etc/envoy/bidding_auction_servers_descriptor_set.pb -v $(pwd)/logs:/logs -v $PROJECT_PATH/bidding-auction-server/tools/debug/envoy.yaml:/tmp/envoy.yaml envoyproxy/envoy:dev-3b18bc650237ce923176becc1e7ee0bd8de4b701 -c /tmp/envoy.yaml
    ```

    You can then send the request to envoy using CURL -

    ```bash
    curl --url localhost:51052/v1/selectAd -H 'X-BnA-Client-IP:<Valid IP address>' -H 'X-User-Agent:Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/111.0.0.0 Safari/537.36' -H 'x-accept-language: en-US,en;q=0.9' -d "@select_ads_request.json"
    ```

Notes:

-   Change log level with the GLOG_v environment variable before starting the servers. To display
    all logs, set the level to 10.
-   You can only connect single instances of each server locally without a load balancer.
-   If you need to change the default port numbers for any servers, make sure and update the scripts
    for the server connecting to this updated server as well. For example, if the port for BFE
    changes, the address must also be updated in tools/debug/start_sfe.
-   Start Bidding server before BuyerFrontEnd server, so that BuyerFrontEnd can connect to Bidding
    on startup. Otherwise, requests might fail.
-   Start Auction server before SellerFrontEnd server, so that SellerFrontEnd can connect to Auction
    on startup. Otherwise, requests might fail.
-   Start Buyer stack before seller stack, so that SellerFrontEnd server can connect to buyer stack
    on startup. Otherwise, requests might fail.

[secure_invoke]:
    https://github.com/privacysandbox/bidding-auction-servers/tree/main/tools/secure_invoke
[grpcurl]: https://github.com/fullstorydev/grpcurl
[envoy]: https://github.com/envoyproxy/envoy
