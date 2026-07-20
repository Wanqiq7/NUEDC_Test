from nuedc_web_gateway.proto_runtime import load_messages_module


def test_loads_generated_envelope():
    messages = load_messages_module()
    envelope = messages.Envelope(sequence=7)

    assert envelope.sequence == 7
