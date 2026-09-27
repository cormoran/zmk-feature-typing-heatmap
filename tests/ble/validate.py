"""Validate semantic Studio responses from the BabbleSim host's raw log."""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


def host_frames(text: str) -> dict[int, bytes]:
    frames: dict[int, bytes] = {}
    current = None
    data = bytearray()
    for line in text.splitlines():
        if not line.startswith("d_02:"):
            continue
        marker = re.search(r"\[RPC RESPONSE (\d+)\]", line)
        if marker:
            if current is not None:
                frames[current] = bytes(data)
            current = int(marker.group(1))
            if current in frames:
                raise AssertionError(f"Duplicate response marker {current}")
            data = bytearray()
        elif current is not None:
            chunk = re.search(r"\s((?:[0-9a-fA-F]{2}\s+){1,16})\|", line)
            if chunk:
                data.extend(bytes.fromhex(chunk.group(1)))
    if current is not None:
        frames[current] = bytes(data)
    return frames


def assert_subset(actual, expected, path="response"):
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            raise AssertionError(f"{path}: expected object, got {actual!r}")
        for key, value in expected.items():
            if key not in actual:
                raise AssertionError(f"{path}.{key}: missing")
            assert_subset(actual[key], value, f"{path}.{key}")
    elif actual != expected:
        raise AssertionError(f"{path}: expected {expected!r}, got {actual!r}")


def message_dict(message):
    from google.protobuf import json_format

    options = {"preserving_proto_field_name": True}
    try:
        return json_format.MessageToDict(
            message, always_print_fields_with_no_presence=True, **options
        )
    except TypeError:
        return json_format.MessageToDict(
            message, including_default_value_fields=True, **options
        )


def validate(output: Path, case: Path, module: Path):
    commands = Path(
        subprocess.check_output(
            ["west", "list", "zmk-west-commands", "-f", "{abspath}"], text=True
        ).strip()
    )
    sys.path.insert(0, str(commands / "scripts/lib/ble"))
    import studio_requests
    from google.protobuf import descriptor_pool, message_factory

    studio = studio_requests.load_workspace_studio_pb2()
    studio_requests.compile_module_protos(module)
    descriptor = descriptor_pool.Default().FindMessageTypeByName(
        "cormoran.feature_typing_heatmap.Response"
    )
    if hasattr(message_factory, "GetMessageClass"):
        response_type = message_factory.GetMessageClass(descriptor)
    else:
        response_type = message_factory.MessageFactory().GetPrototype(descriptor)
    expectations = json.loads((case / "studio_expectations.json").read_text())
    frames = host_frames(output.read_text())
    expected_ids = {entry["request_id"] for entry in expectations}
    if set(frames) != expected_ids:
        raise AssertionError(
            f"Expected response IDs {sorted(expected_ids)}, got {sorted(frames)}"
        )
    for entry in expectations:
        request_id = entry["request_id"]
        envelope = studio.Response()
        envelope.ParseFromString(frames[request_id])
        if envelope.WhichOneof("type") != "request_response":
            raise AssertionError(f"Response {request_id} is not a request response")
        response = envelope.request_response
        if (
            response.request_id != request_id
            or response.WhichOneof("subsystem") != "custom"
        ):
            raise AssertionError(f"Unexpected envelope for request {request_id}")
        custom = response.custom
        if "custom" in entry:
            assert_subset(
                message_dict(custom), entry["custom"], f"response {request_id}"
            )
        else:
            if (
                custom.WhichOneof("response_type") != "call"
                or custom.call.subsystem_index != 0
            ):
                raise AssertionError(
                    f"Expected heatmap call response for request {request_id}"
                )
            inner = response_type()
            inner.ParseFromString(custom.call.payload)
            assert_subset(
                message_dict(inner), entry["module"], f"response {request_id}"
            )
    print(f"BLE heatmap semantic responses: PASS ({len(expectations)} requests)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    module = Path(__file__).resolve().parents[2]
    topdir = Path(subprocess.check_output(["west", "topdir"], text=True).strip())
    output = args.output or topdir / "build/ble/studio/custom-rpc-split/output.log"
    validate(output, Path(__file__).parent / "studio/custom-rpc-split", module)


if __name__ == "__main__":
    main()
