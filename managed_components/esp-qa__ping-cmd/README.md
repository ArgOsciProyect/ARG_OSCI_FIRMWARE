# Ping Command

[![Component Registry](https://components.espressif.com/components/esp-qa/ping-cmd/badge.svg)](https://components.espressif.com/components/esp-qa/ping-cmd)

This repository contains `ping` commands based esp-idf console.

## Documentation

- Use `help` for overview of commands:

  ```
  ping  [-W <timeout>] [-i <interval>] [-s <size>] [-c <count>] [-Q <n>] [[host]] [--abort]
    send ICMP ECHO_REQUEST to network hosts
    -W, --timeout=<timeout>  time to wait for response
    -i, --interval=<interval>  seconds between sending each packet
    -s, --size=<size>  use <size> as number of data bytes to be sent
    -c, --count=<count>  stop after <count> replies, default: 5
    -Q, --tos=<n>  Set Type of Service related bits in IP datagrams
          [host]  domain name or ip address
        --abort  ping abort
  ```

### Installation

- To add a plugin command or any component from IDF component manager into your project, simply include an entry within the `idf_component.yml` file.

  ```yaml
  dependencies:
    esp-qa/ping-cmd:
      version: "^0.0.1"
  ```
- For more details refer [IDF Component Manager](https://docs.espressif.com/projects/idf-component-manager/en/latest/)
