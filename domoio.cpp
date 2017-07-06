#include <Arduino.h>
#include "domoio.h"
#include "cantcoap.h"
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include "storage.h"

#define URI_BUFFER_LENGTH 25

#define BUFFER_SIZE 512

// #define DOMOIO_URL "https://app.domoio.com"
#define DOMOIO_URL "http://10.254.0.200:4000"

byte buffer[BUFFER_SIZE];
bool session_started = false;

WiFiClientSecure client;

const char* host = "10.254.0.200";
const int port = 1234;

int message_id_counter = 0;

void clear_buffer() {
  memset(buffer, 0, BUFFER_SIZE);
}

bool is_connected() {
  return client.connected();
}

int receive() {
  if (!client.available()) return -1;

  int size_buf[2];
  size_buf[0] = client.read();
  size_buf[1] = client.read();

  int size = size_buf[1] | size_buf[0] << 8;
  if (size > BUFFER_SIZE) {
    Serial.println("ERROR: OVERFLOW");
    return -1;
  }
  clear_buffer();
  for (int i=0; i < size; i++) {
    buffer[i] = client.read();
  }

  return size;
}


int block_until_receive() {
  int size = -1;
  double start = millis();
  while((size = receive()) == -1 && start + 5000 > millis()) {
    delay(250);
  }
  return size;
}


const char *expected = "HELLO";
int expected_len = strlen(expected);

bool handsake() {
  char device_id[37];
  if (Storage::get_device_id(&device_id[0], 37) == -1) {
    Serial.println("Error reading device id");
    return false;
  }
  Serial.print("Device_id: ");
  Serial.println(&device_id[0]);

  send(&device_id[0], 36);

  // Read the encryptd nounce
  int size = block_until_receive();


  byte nounce_clean[size];
  decrypt(&buffer[0], &nounce_clean[0], size);
  send(&nounce_clean, 40);
  // Serial.print("Received: ");
  // Serial.print(size);
  // Serial.print(" => ");
  // Serial.println((char *) &buffer[0]);

  // Read the encryptd nounce
  size = block_until_receive();


  if (size != expected_len || strncmp(expected, (char *) &buffer[0], size) != 0) {
    Serial.println("BAD LOGIN");
    return false;
  }

  session_started = true;
  Serial.println("Session started");

  return true;
}

void connect() {
  Serial.println("connecting");
  if (!client.connect(host, port)) {
    Serial.println("connection failed");
    return;
  }

  handsake();
}




int send(const void* data, int size) {

  int packet_size = size + 2;
  byte buffer[packet_size];

  buffer[0] = (byte) ((size >> 8) & 0xFF);
  buffer[1] = (byte) (size & 0xFF);

  memcpy(&buffer[2], data, size);

  return client.write(&buffer[0], packet_size);
}

int send_confirmation(CoapPDU *msg) {
  CoapPDU reply;

	reply.setType(CoapPDU::COAP_ACKNOWLEDGEMENT);
	reply.setCode(msg->getCode());
	reply.setToken(msg->getTokenPointer(), msg->getTokenLength());
	reply.setMessageID(msg->getMessageID());
  return send(reply.getPDUPointer(), reply.getPDULength());
}

void process_message(CoapPDU *msg) {
  char uri_buf[URI_BUFFER_LENGTH];
  int uri_size;
  msg->getURI(&uri_buf[0], URI_BUFFER_LENGTH, &uri_size);
  Serial.print("URI: ");
  Serial.println(uri_buf);


  int payload_length = msg->getPayloadLength();
  uint8_t *payload = msg->getPayloadPointer();
  Serial.print("Payload ");
  Serial.println(payload_length);

  int port_id = buff2i(payload, 0);
  int value = buff2i(payload, 2);
  Serial.print("Port: ");
  Serial.println(port_id);

  Serial.print("Value: ");
  Serial.println(value);

  if (value == 1) {
    digitalWrite(LED_BUILTIN, LOW);
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
  }


  // Send the response
  send_confirmation(msg);
}


void receive_messages() {
  int size = receive();

  if (size <= 0) {
    return;
  }

  // Serial.print("Received: ");
  // Serial.println(size);

  CoapPDU coap_msg = CoapPDU(buffer, size);

  if (!coap_msg.validate()) {
    Serial.print("Bad packet rc");
    return;
  }

  process_message(&coap_msg);
}



int next_message_id() {
  int id = message_id_counter++;
  return id;
}

int Message::send() {
  CoapPDU msg;

  // LOG
  if (this->action == ACTION_LOG) {
    msg.setType(CoapPDU::COAP_CONFIRMABLE);
    msg.setCode(CoapPDU::COAP_POST);
    msg.setURI("/log");
  }

  // Default settings
  else {

    msg.setType(CoapPDU::COAP_NON_CONFIRMABLE);
    msg.setCode(CoapPDU::COAP_POST);
  }



  msg.setMessageID(next_message_id());
  msg.setPayload(this->payload, this->payload_len);

  return ::send(msg.getPDUPointer(), msg.getPDULength());
}



void remote_log(const char *data) {
  Message msg(ACTION_LOG, data);
  msg.send();
}


bool register_device(String claim_code, String public_key) {
  HTTPClient http;
  String url(DOMOIO_URL);
  bool success = false;
  url += "/api/register_device";
  Serial.println(url);
  http.begin(url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String post_data("");
  post_data += "claim_code=" + claim_code + "&public_key=" + public_key + "&hardware_id=" + String(ESP.getChipId());
  int resp_code = http.POST(post_data);
  if (resp_code > 0) {
    String resp = http.getString();
    char device_id[37];
    sscanf(resp.c_str(), "{\"device_id\":\"%36s\"}", &device_id[0]);

    Serial.println(&device_id[0]);

    // Save the device_id
    Storage::set_device_id(&device_id[0]);

    success = true;
  } else {
    Serial.print("ERROR: ");
    Serial.println(http.errorToString(resp_code));
  }

  http.end();
  return success;
}
