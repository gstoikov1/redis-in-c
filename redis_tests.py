#!/usr/bin/env python3

import argparse
import os
import socket
import sys
import time


PONG = b"+PONG\r\n"
OK = b"+OK\r\n"
NULL_BULK_STRING = b"$-1\r\n"
TIMEOUT_SECONDS = 2
PING_COUNT = 5


def bulk_string(value):
    if isinstance(value, str):
        value = value.encode()

    return (
        b"$"
        + str(len(value)).encode()
        + b"\r\n"
        + value
        + b"\r\n"
    )


def command(*parts):
    return (
        b"*"
        + str(len(parts)).encode()
        + b"\r\n"
        + b"".join(bulk_string(part) for part in parts)
    )


def receive_exact(sock, expected_size):
    data = bytearray()

    while len(data) < expected_size:
        chunk = sock.recv(expected_size - len(data))

        if not chunk:
            raise RuntimeError("Server closed the connection")

        data.extend(chunk)

    return bytes(data)


def receive_line(sock):
    data = bytearray()

    while not data.endswith(b"\r\n"):
        byte = sock.recv(1)
        if not byte:
            raise RuntimeError("Server closed the connection before CRLF")
        data.extend(byte)

    return bytes(data)


def send_and_expect(sock, request, expected):
    sock.sendall(request)
    response = receive_exact(sock, len(expected))

    if response != expected:
        raise AssertionError(
            f"Expected {expected!r}, received {response!r}"
        )


def send_and_read_integer(sock, request):
    sock.sendall(request)
    response = receive_line(sock)

    if not response.startswith(b":"):
        raise AssertionError(f"Expected RESP integer, received {response!r}")

    try:
        return int(response[1:-2])
    except ValueError as error:
        raise AssertionError(f"Invalid RESP integer: {response!r}") from error


def test_step_2(host, port):
    with socket.create_connection((host, port), timeout=TIMEOUT_SECONDS) as sock:
        send_and_expect(sock, command(b"PING"), PONG)


def test_step_3(host, port):
    expected = PONG * PING_COUNT

    with socket.create_connection((host, port), timeout=TIMEOUT_SECONDS) as sock:
        sock.sendall(command(b"PING") * PING_COUNT)
        response = receive_exact(sock, len(expected))

        if response != expected:
            raise AssertionError(
                f"Expected {expected!r}, received {response!r}"
            )


def test_step_4(host, port):
    with socket.create_connection((host, port), timeout=TIMEOUT_SECONDS) as idle:
        # Give the server time to accept the idle connection.
        time.sleep(0.2)

        with socket.create_connection((host, port), timeout=TIMEOUT_SECONDS) as active:
            start = time.monotonic()
            send_and_expect(active, command(b"PING"), PONG)
            elapsed = time.monotonic() - start

        # The first connection must still be usable.
        send_and_expect(idle, command(b"PING"), PONG)

    return f"active client responded in {elapsed:.3f} seconds"


def test_step_5(host, port):
    with socket.create_connection((host, port), timeout=TIMEOUT_SECONDS) as sock:
        send_and_expect(
            sock,
            command(b"ECHO", b"hello"),
            b"$5\r\nhello\r\n",
        )
        send_and_expect(
            sock,
            command(b"ECHO", b""),
            b"$0\r\n\r\n",
        )


def test_step_6(host, port):
    key = f"test:key:{os.getpid()}".encode()
    missing_key = f"test:missing:{os.getpid()}".encode()
    value = b"Gabriel"

    with socket.create_connection((host, port), timeout=TIMEOUT_SECONDS) as sock:
        send_and_expect(sock, command(b"SET", key, value), OK)
        send_and_expect(sock, command(b"GET", key), bulk_string(value))
        send_and_expect(sock, command(b"GET", missing_key), NULL_BULK_STRING)


def test_step_7(host, port):
    key = f"test:expiry:{os.getpid()}".encode()
    value = b"temporary"
    ttl_milliseconds = 300

    with socket.create_connection((host, port), timeout=TIMEOUT_SECONDS) as sock:
        send_and_expect(
            sock,
            command(
                b"SET",
                key,
                value,
                b"PX",
                str(ttl_milliseconds).encode(),
            ),
            OK,
        )

        time.sleep(0.5)
        send_and_expect(sock, command(b"GET", key), NULL_BULK_STRING)


def test_step_8(host, port):
    key = f"test:delete:{os.getpid()}".encode()
    absent_key = f"test:absent:{os.getpid()}".encode()

    with socket.create_connection((host, port), timeout=TIMEOUT_SECONDS) as sock:
        send_and_expect(sock, command(b"SET", key, b"value"), OK)
        if send_and_read_integer(sock, command(b"EXISTS", key)) != 1:
            raise AssertionError("EXISTS should return 1 for a present key")
        if send_and_read_integer(sock, command(b"EXISTS", absent_key)) != 0:
            raise AssertionError("EXISTS should return 0 for a missing key")
        if send_and_read_integer(sock, command(b"DEL", key)) != 1:
            raise AssertionError("DEL should return 1 when it deletes a key")
        if send_and_read_integer(sock, command(b"DEL", key)) != 0:
            raise AssertionError("DEL should return 0 when the key is already absent")


def test_step_9(host, port):
    persistent_key = f"test:ttl:persistent:{os.getpid()}".encode()
    expiring_key = f"test:ttl:expiring:{os.getpid()}".encode()
    missing_key = f"test:ttl:missing:{os.getpid()}".encode()

    with socket.create_connection((host, port), timeout=TIMEOUT_SECONDS) as sock:
        send_and_expect(sock, command(b"SET", persistent_key, b"value"), OK)
        if send_and_read_integer(sock, command(b"TTL", persistent_key)) != -1:
            raise AssertionError("TTL should return -1 for a persistent key")
        if send_and_read_integer(sock, command(b"PTTL", persistent_key)) != -1:
            raise AssertionError("PTTL should return -1 for a persistent key")
        if send_and_read_integer(sock, command(b"TTL", missing_key)) != -2:
            raise AssertionError("TTL should return -2 for a missing key")
        if send_and_read_integer(sock, command(b"PTTL", missing_key)) != -2:
            raise AssertionError("PTTL should return -2 for a missing key")

        ttl_milliseconds = 2000
        send_and_expect(
            sock,
            command(b"SET", expiring_key, b"value", b"PX", str(ttl_milliseconds).encode()),
            OK,
        )
        pttl = send_and_read_integer(sock, command(b"PTTL", expiring_key))
        ttl = send_and_read_integer(sock, command(b"TTL", expiring_key))

        if not 0 < pttl <= ttl_milliseconds:
            raise AssertionError(f"PTTL returned unexpected value {pttl}")
        if not 0 <= ttl <= 2:
            raise AssertionError(f"TTL returned unexpected value {ttl}")


def test_step_10(host, port):
    key = f"test:ttl:reset:{os.getpid()}".encode()

    with socket.create_connection((host, port), timeout=TIMEOUT_SECONDS) as sock:
        send_and_expect(
            sock,
            command(b"SET", key, b"temporary", b"PX", b"350"),
            OK,
        )
        send_and_expect(sock, command(b"SET", key, b"permanent"), OK)
        time.sleep(0.5)
        send_and_expect(sock, command(b"GET", key), bulk_string(b"permanent"))


def test_step_11(host, port):
    key = b"test:binary:\x00key"
    value = b"\x00hello\r\n\xff"

    with socket.create_connection((host, port), timeout=TIMEOUT_SECONDS) as sock:
        send_and_expect(sock, command(b"SET", key, value), OK)
        send_and_expect(sock, command(b"GET", key), bulk_string(value))
        send_and_expect(sock, command(b"ECHO", value), bulk_string(value))


TESTS = {
    "2": ("Respond to PING", test_step_2),
    "3": ("Handle multiple PINGs", test_step_3),
    "4": ("Handle concurrent clients", test_step_4),
    "5": ("Implement ECHO", test_step_5),
    "6": ("Implement SET and GET", test_step_6),
    "7": ("Add expiry", test_step_7),
    "8": ("Implement DEL and EXISTS", test_step_8),
    "9": ("Implement TTL and PTTL", test_step_9),
    "10": ("Clear expiry when SET overwrites a key", test_step_10),
    "11": ("Support binary-safe keys and values", test_step_11),
}


def main():
    parser = argparse.ArgumentParser(
        description="Run Redis roadmap tests"
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6379)
    parser.add_argument(
        "--step",
        choices=tuple(TESTS) + ("all",),
        default="all",
        help="Run one step, or all tests by default",
    )
    args = parser.parse_args()

    selected_steps = list(TESTS) if args.step == "all" else [args.step]
    passed = 0
    failed = 0

    print(f"Redis test report for {args.host}:{args.port}")
    print("-" * 50)

    for step in selected_steps:
        name, test = TESTS[step]

        try:
            details = test(args.host, args.port)
        except (OSError, RuntimeError, AssertionError) as error:
            failed += 1
            print(f"[FAIL] Step {step}: {name} — {error}")
        else:
            passed += 1
            print(f"[PASS] Step {step}: {name}")
            if details:
                print(f"       {details}")

    print("-" * 50)
    print(f"Summary: {passed} passed, {failed} failed")

    if failed:
        print("RESULT: FAILURE")
        return 1

    print("RESULT: SUCCESS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
