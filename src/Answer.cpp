#include "Answer.h"

#include <string.h>
#include <stdlib.h>

namespace mDNSResolver {
  Answer::Answer() {
    this->name = NULL;
    this->data = NULL;
  }

  Answer::~Answer() {
    if(this->name) {
      free(this->name);
    }
    if(this->data) {
      free(this->data);
    }
  }

  MDNS_RESULT Answer::parseAnswer(unsigned char* buffer, unsigned int len, unsigned int* offset, Answer* answer) {
    char* assembled = (char *)malloc(sizeof(char) * MDNS_MAX_NAME_LEN);
    int nameLen = Answer::assembleName(buffer, len, offset, &assembled);

    if(nameLen == -1 * E_MDNS_POINTER_OVERFLOW) {
      free(assembled);
      return nameLen;
    }

    answer->name = (char *)malloc(sizeof(char) * nameLen);
    parseName(&answer->name, assembled, strlen(assembled));
    free(assembled );

    answer->type = (buffer[(*offset)++] << 8) + buffer[(*offset)++];
    answer->aclass = buffer[(*offset)++];
    answer->cacheflush = buffer[(*offset)++];

    answer->ttl = (buffer[(*offset)++] << 24) + (buffer[(*offset)++] << 16) + (buffer[(*offset)++] << 8) + buffer[(*offset)++];
    answer->len = (buffer[(*offset)++] << 8) + buffer[(*offset)++];

    //if(answer->type == MDNS_A_RECORD) {
      //answer->data = (byte *)malloc(sizeof(byte) * answer->len);
      //memcpy(answer->data, buffer + (*offset), answer->len);
      //(*offset) += answer->len;
    //if(answer->type == MDNS_CNAME_RECORD) {
      //unsigned int dataOffset = (*offset);
      //(*offset) += answer->len;
      //Answer::assembleName(buffer, len, &dataOffset, &assembled, answer->len);
      //answer->len = parseName(answer->data, assembled, strlen(assembled));
    //} else {
      //// Not an A record or a CNAME. Ignore.
    //}
    return E_MDNS_OK;
  }

  // Response should have the name set already
  MDNS_RESULT Answer::buildResponse(unsigned char* buffer, unsigned int len, Response& response) {
    if((buffer[2] & 0b10000000) != 0b10000000) {
      // Not an answer packet
      return E_MDNS_OK;
    }

    if(buffer[2] & 0b00000010) {
      // Truncated - we don't know what to do with these
      return E_MDNS_TRUNCATED;
    }

    if (buffer[3] & 0b00001111) {
      return E_MDNS_PACKET_ERROR;
    }

    unsigned int answerCount = (buffer[6] << 8) + buffer[7];

    // For this library, we are only interested in packets that contain answers
    if(answerCount == 0) {
      return E_MDNS_OK;
    }

    unsigned int offset = 0;

    MDNS_RESULT questionResult = skipQuestions(buffer, len, &offset);
    if(questionResult != E_MDNS_OK) {
      return questionResult;
    }

    Answer* answers = new Answer[answerCount];
    for(int i = 0; i < answerCount; i++) {
      parseAnswer(buffer, len, &offset, answers + i);
    }

    delete[] answers;
    return E_MDNS_OK;
  }

  // Converts a encoded DNS name into a FQDN.
  // name: pointer to char array where the result will be stored. Needs to have already been allocated. It's allocated length should be len - 1
  // mapped: The encoded DNS name
  // len: Length of mapped
  MDNS_RESULT Answer::parseName(char** name, const char* mapped, unsigned int len) {
    unsigned int namePointer = 0;
    unsigned int mapPointer = 0;

    while(mapPointer < len) {
      int labelLength = mapped[mapPointer++];

      if(namePointer + labelLength > len - 1) {
        return E_MDNS_INVALID_LABEL_LENGTH;
      }

      if(namePointer != 0) {
        (*name)[namePointer++] = '.';
      }

      for(int i = 0; i < labelLength; i++) {
        (*name)[namePointer++] = mapped[mapPointer++];
      }
    }

    (*name)[len - 1] = '\0';

    return E_MDNS_OK;
  }

  int Answer::assembleName(unsigned char *buffer, unsigned int len, unsigned int *offset, char **name, unsigned int maxlen) {
    unsigned int index = 0;

    while(index < maxlen) {
      if((buffer[*offset] & 0xc0) == 0xc0) {
        unsigned int pointerOffset = ((buffer[(*offset)++] & 0x3f) << 8) + buffer[*offset];
        if(pointerOffset > len) {
          // Points to somewhere beyond the packet
          return -1 * E_MDNS_POINTER_OVERFLOW;
        }

        char *namePointer = *name + index;
        int pointerLen = assembleName(buffer, len, &pointerOffset, &namePointer, maxlen - index);

        if(pointerLen < 0) {
          return pointerLen;
        }

        index += pointerLen;

        break;
      } else if(buffer[*offset] == '\0') {
        (*name)[index++] = buffer[(*offset)++];
        break;
      } else {
        (*name)[index++] = buffer[(*offset)++];
      }
    }

    return index;
  }

  int Answer::assembleName(unsigned char *buffer, unsigned int len, unsigned int *offset, char **name) {
    return assembleName(buffer, len, offset, name, MDNS_MAX_NAME_LEN);
  }

  // Work out how many bytes are dedicated to questions. Since we aren't answering questions, they can be skipped
  // buffer: The mDNS packet we are parsing
  // len: Length of the packet
  // offset: the byte we are up to in the parsing process
  MDNS_RESULT Answer::skipQuestions(unsigned char* buffer, unsigned int len, unsigned int* offset) {
    unsigned int questionCount = (buffer[4] << 8) + buffer[5];

    *offset += 12;
    for(int i = 0; i < questionCount; i++) {

      while(buffer[*offset] != '\0') {
        // If it's a pointer, add two to the counter
        if((buffer[*offset] & 0xc0) == 0xc0) {
          (*offset) += 2;
          break;
        } else {
          unsigned int labelSize = (unsigned int)buffer[*offset];

          if(labelSize > 0x3f) {
            return E_MDNS_PACKET_ERROR;
          }

          (*offset) += 1; // Increment to move to the next byte
          (*offset) += labelSize;

          if(*offset > len) {
            return E_MDNS_PACKET_ERROR;
          }
        }
      }

      (*offset) += 5; // 2 bytes for the qtypes and 2 bytes qclass + plus one to land us on the next bit
    }

    if(*offset > len + 1) {
      return E_MDNS_PACKET_ERROR;
    }

    return E_MDNS_OK;
  }
};
