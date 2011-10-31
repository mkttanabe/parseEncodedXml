/*
 *  parseEncodedXml.c
 *
 *  apk �ւ̃p�b�P�[�W���O�̍ۂɃG���R�[�h���ꂽ AndroidManifest.xml ��ǂ�
 *
 *  �g����: parseEncodedXml [�G���R�[�h���� XML �t�@�C����]
 *
 *  Copyright(c) 2011 KLab Inc.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <sys/stat.h>
//#include <wchar.h>

#ifdef _MSC_VER
typedef signed __int8 int8_t;
typedef signed __int16 int16_t;
typedef signed __int32 int32_t;
typedef signed __int64 int64_t;
typedef unsigned __int8 uint8_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
#else
#include <stdint.h>
#endif

typedef unsigned char byte;

// �ȉ��̒萔�ƍ\���̂̒�`��
// frameworks/base/include/utils/ResourceTypes.h ����̔���

enum {
	RES_NULL_TYPE                = 0x0000,
	RES_STRING_POOL_TYPE         = 0x0001,
	RES_TABLE_TYPE               = 0x0002,
	RES_XML_TYPE                 = 0x0003,
	RES_XML_FIRST_CHUNK_TYPE     = 0x0100,
	RES_XML_START_NAMESPACE_TYPE = 0x0100,
	RES_XML_END_NAMESPACE_TYPE   = 0x0101,
	RES_XML_START_ELEMENT_TYPE   = 0x0102,
	RES_XML_END_ELEMENT_TYPE     = 0x0103,
	RES_XML_CDATA_TYPE           = 0x0104,
	RES_XML_LAST_CHUNK_TYPE      = 0x017f,
	RES_XML_RESOURCE_MAP_TYPE    = 0x0180,
	RES_TABLE_PACKAGE_TYPE       = 0x0200,
	RES_TABLE_TYPE_TYPE          = 0x0201,
	RES_TABLE_TYPE_SPEC_TYPE     = 0x0202
};

struct ResChunk_header {
	uint16_t type;
	uint16_t headerSize;
	uint32_t size;
};

enum {
  SORTED_FLAG = 1<<0,
  UTF8_FLAG = 1<<8
};

struct ResStringPool_header {
	struct ResChunk_header header;
	uint32_t stringCount;
	uint32_t styleCount;
	uint32_t flags;
	uint32_t stringsStart;
	uint32_t stylesStart;
};

struct ResXMLTree_header {
	struct ResChunk_header header;
};

struct ResStringPool_ref {
	uint32_t index;
};

struct ResXMLTree_node {
	struct ResChunk_header header;
	uint32_t lineNumber;
	struct ResStringPool_ref comment;
};

struct ResXMLTree_namespaceExt {
	struct ResStringPool_ref prefix;
	struct ResStringPool_ref uri;
};

struct ResXMLTree_attrExt {
	struct ResStringPool_ref ns;
	struct ResStringPool_ref name;
	uint16_t attributeStart;
	uint16_t attributeSize;
	uint16_t attributeCount;
	uint16_t idIndex;
	uint16_t classIndex;
	uint16_t styleIndex;
};

enum {
	TYPE_NULL = 0x00,
	TYPE_REFERENCE = 0x01,
	TYPE_ATTRIBUTE = 0x02,
	TYPE_STRING = 0x03,
	TYPE_FLOAT = 0x04,
	TYPE_DIMENSION = 0x05,
	TYPE_FRACTION = 0x06,
	TYPE_FIRST_INT = 0x10,
	TYPE_INT_DEC = 0x10,
	TYPE_INT_HEX = 0x11,
	TYPE_INT_BOOLEAN = 0x12,
	TYPE_FIRST_COLOR_INT = 0x1c,
	TYPE_INT_COLOR_ARGB8 = 0x1c,
	TYPE_INT_COLOR_RGB8 = 0x1d,
	TYPE_INT_COLOR_ARGB4 = 0x1e,
	TYPE_INT_COLOR_RGB4 = 0x1f,
	TYPE_LAST_COLOR_INT = 0x1f,
	TYPE_LAST_INT = 0x1f
};

struct Res_value {
	uint16_t size;
	uint8_t res0;
	uint8_t dataType;
	uint32_t data;
};

struct ResXMLTree_attribute {
	struct ResStringPool_ref ns;
	struct ResStringPool_ref name;
	struct ResStringPool_ref rawValue;
	struct Res_value typedValue;
};

struct ResXMLTree_endElementExt {
	struct ResStringPool_ref ns;
	struct ResStringPool_ref name;
};


char *dump(void *p, int len) {
  byte *buffer = (byte*)p;
  static char buf[80];
  int i, cnt = 0;
  for (i = 0; i < len; i++) {
	sprintf(&buf[cnt], "%02X ", buffer[i]);
	cnt += 3;
	if ((i+1) % 8 == 0) {
	  strcat(buf, " ");
	  cnt++;
	}
	if ((i+1) % 16 == 0) {
	  printf("%s\n", buf);
	  cnt = 0;
	  buf[0] = '\0';
	}
  }
  return buf;
}

char *makeAttrValue(uint8_t dataType, uint32_t data) {
	static char fmt[32];

	switch (dataType) {
	case TYPE_NULL:
		sprintf(fmt, "");
		break;
	case TYPE_REFERENCE:
		sprintf(fmt, "@0x%08X", data);
		break;
	case TYPE_INT_DEC:
		sprintf(fmt, "%d", data);
		break;
	case TYPE_INT_HEX:
		sprintf(fmt, "0x%08X", data);
		break;
	case TYPE_INT_BOOLEAN:
		sprintf(fmt, "%s", (data == 0) ? "false" : "true");
		break;
	default: // ���͂Ƃ肠�����K��
		sprintf(fmt, "0x%08X", data);
		break;
	}
	return fmt;
}

char *myDup(byte *data, int len) {
	int i;
	char *p = (char *)malloc(len+1);
	for (i = 0; i < len; i++) {
		p[i] = data[i*2];
	}
	p[len] = '\0';
	return p;
}

int doParse(int ac, char *av[]) {

	FILE *fp = NULL;
	int i, ret, num, depth, inTag;
	int stringCount = 0;
	uint32_t val32;
	uint16_t val16;
	size_t size;
	byte *bp, *p = NULL;
	char **ppStrArray = NULL;
	struct stat st;
	struct ResChunk_header h,       *rchp;
	struct ResStringPool_header     *rsph;
	struct ResXMLTree_header        *rxth;
	struct ResXMLTree_node          *rxtn;
	struct ResXMLTree_namespaceExt  *rxnse;
	struct ResXMLTree_attrExt       *rxtae;
	struct ResXMLTree_attribute     *rxta;
	struct Res_value                *rv;
	struct ResXMLTree_endElementExt *rxteee;

	if (ac < 2) {
		printf("usage: %s [Encoded XML-file]\n", av[0]);
		return 0;
	}
	if (stat(av[1], &st) < 0 || (fp = fopen(av[1], "rb")) == NULL) {
		printf("can't open %s\n", av[1]);
		return -1;
	}
	if (fread(&h, sizeof(h), 1, fp) < 1) {
		printf("can't read %s\n", av[1]);
		ret = -2;
		goto DONE;
	}
	// �w�b�_�̐��������`�F�b�N
	if (h.type != RES_XML_TYPE    ||
		h.headerSize != sizeof(h) ||
		h.size != (uint32_t)st.st_size) {
		printf("invalid data\n");
		ret = -3;
		goto DONE;
	}
	// �ۂ��ƃ������֓ǂݍ���
	rewind(fp);
	if ((p = (byte*)malloc(st.st_size)) == NULL ||
		fread(p, st.st_size, 1, fp) < 1) {
		printf("buffering error\n");
		ret = -4;
		goto DONE;
	}
	fclose(fp);
	fp = NULL;

	printf("[1.�t�@�C���w�b�_]\n");
	rchp = (struct ResChunk_header*)p;
	printf("%s: ���ʎq = 0x%04X\n",
			dump(&rchp->type, 2), rchp->type);
	printf("%s: �{�w�b�_�T�C�Y = %d byte\n",
			dump(&rchp->headerSize, 2), rchp->headerSize);
	printf("%s: �t�@�C���T�C�Y = %d byte\n",
			dump(&rchp->size, 4), rchp->size);
	p += sizeof(struct ResChunk_header);
	putchar('\n');

	printf("[2.������v�[���w�b�_]\n");
	rsph = (struct ResStringPool_header*)p;
	rchp = &rsph->header;
	printf("%s: ���ʎq = 0x%04X\n",
			dump(&rchp->type, 2), rchp->type);
	printf("%s: �{�w�b�_�T�C�Y = %d byte\n",
			dump(&rchp->headerSize, 2), rchp->headerSize);
	printf("%s: �{�w�b�_�{(C)�{(D)�̃T�C�Y = %d byte\n",
			dump(&rchp->size, 4), rchp->size);
	printf("%s: ������v�[���̕ێ����镶����f�[�^�� = %d\n",
			dump(&rsph->stringCount, 4), rsph->stringCount);
	stringCount = rsph->stringCount;
	printf("%s: ������v�[���̕ێ����� style span arrays �̐� = %d\n",
			dump(&rsph->styleCount, 4), rsph->styleCount);
	if (rsph->styleCount != 0) {
		printf("style �f�[�^�̏����ɂ͑Ή����Ă��܂���\n");
		ret = -5;
		goto DONE;
	}
	printf("%s: �t���O = 0x%08X\n",
			dump(&rsph->flags, 4), rsph->flags);
	if (rsph->flags & UTF8_FLAG) {
		printf("UTF-8 �̕�����v�[���ɂ͑Ή����Ă��܂���\n");
		ret = -5;
		goto DONE;
	}
	printf("%s: ������v�[���̊J�n�I�t�Z�b�g = 0x%08X (%d)\n",
			dump(&rsph->stringsStart, 4),
			rsph->stringsStart, rsph->stringsStart);
	printf("%s: style ������v�[���̊J�n�I�t�Z�b�g = 0x%08X (%d)\n",
			dump(&rsph->stylesStart, 4),
			rsph->stylesStart, rsph->stylesStart);
	p += sizeof(struct ResStringPool_header);
	putchar('\n');

	printf("[2-1.������v�[����̊e�G���g���̊J�n�ʒu���F�ϒ�] (C)\n");
	for (i = 0; i < (int)rsph->stringCount; i++) {
		val32 = *((uint32_t*)(p+(i*sizeof(uint32_t))));
		printf("%s: [%02d] = 0x%08X (%d)\n",
				dump(&val32, 4), i, val32, val32);
	}
	putchar('\n');

	printf("[2-2.������v�[���F�ϒ�] (D)\n");

	// �㑱�����ł̎Q�Ƃ̂��߂ɕ�����p�̔z����쐬
	size = rsph->stringCount * sizeof(char*);
	ppStrArray = (char**)malloc(size);
	memset(ppStrArray, 0, size);

	for (i = 0; i < (int)rsph->stringCount; i++) {
		val32 = *((uint32_t*)(p+(i*sizeof(uint32_t))));
		bp = (byte*)rsph + rsph->stringsStart + val32;
		val16 = *((uint16_t*)bp);
		printf("%s: len=%.2d; [%02d]",
				dump(&val16, 2), val16, i);
		ppStrArray[i] = myDup(bp + sizeof(val16), val16);
		printf(" = [%s]\n", ppStrArray[i]);
	}
	putchar('\n');

	p = (byte*)rsph + rsph->header.size;

	printf("[3.XML ���w�b�_]\n");
	rxth = (struct ResXMLTree_header*)p;
	printf("%s: ���ʎq = 0x%04X\n",
			dump(&rxth->header.type, 2), rxth->header.type);
	printf("%s: �{�w�b�_�T�C�Y = %d byte\n",
			dump(&rxth->header.headerSize, 2), rxth->header.headerSize);
	printf("%s: �{�w�b�_�{(E)�̃T�C�Y = %d byte\n",
			dump(&rxth->header.size, 4), rxth->header.size);
	putchar('\n');

	p += rxth->header.headerSize;

	printf("[3-1.XML �g�p���� ID �e�[�u��] (E)\n");
	// �e�[�u����� ID �����v�Z
	num = (rxth->header.size - rxth->header.headerSize) / sizeof(uint32_t);
	for (i = 0; i < num; i++) {
		val32 = *((uint32_t*)(p+(i*sizeof(uint32_t))));
		printf("%s: [%02d] = 0x%08X\n",
				dump(&val32, 4), i, val32);
	}
	putchar('\n');

	p = (byte*)rxth + rxth->header.size;

	printf("[4.XML �c���[�J�n���m�[�h]\n");
	rxtn = (struct ResXMLTree_node*)p;
	printf("%s: ���ʎq = 0x%04X\n",
			dump(&rxtn->header.type, 2), rxtn->header.type);
	printf("%s: �{�m�[�h�̃T�C�Y = %d byte\n",
			dump(&rxtn->header.headerSize, 2), rxtn->header.headerSize);
	printf("%s: �{�m�[�h�{(F)�̃T�C�Y = %d byte\n",
			dump(&rxtn->header.size, 4), rxtn->header.size);
	printf("%s: ���� XML �t�@�C����� XML �L�q�J�n�ʒu�̍s�ԍ� = %d\n",
			dump(&rxtn->lineNumber, 4), rxtn->lineNumber);
	printf("%s: �{�m�[�h�ւ̃R�����g�� ������v�[����̗v�f�ԍ� = %d %s\n",
			dump(&rxtn->comment.index, 4), rxtn->comment.index,
			(rxtn->comment.index == 0xFFFFFFFF) ? "(none)" : "");
	putchar('\n');

	p += rxtn->header.headerSize;

	printf("[4-1.XML ���O��ԏ��m�[�h] (F)\n");
	rxnse = (struct ResXMLTree_namespaceExt*)p;
	printf("%s: XML ���O��Ԑړ����� ������v�[����̗v�f�ԍ� = %d\n",
			dump(&rxnse->prefix.index, 4), rxnse->prefix.index);
	if (rxnse->prefix.index != 0xFFFFFFFF) {
		printf("%*s-> pool[%02d] = [%s]\n", 14, "",
			rxnse->prefix.index, ppStrArray[rxnse->prefix.index]);
	}
	printf("%s: XML ���O��� URI �� ������v�[����̗v�f�ԍ� = %d\n",
			dump(&rxnse->uri.index, 4), rxnse->uri.index);
	if (rxnse->uri.index != 0xFFFFFFFF) {
		printf("%*s-> pool[%02d] = [%s]\n", 14, "",
			rxnse->uri.index, ppStrArray[rxnse->uri.index]);
	}
	putchar('\n');

	p = (byte*)rxtn + rxtn->header.size;

	bp = p;
	for (;;) {
		val16 = *((uint16_t*)bp);

		if (val16 == RES_XML_START_ELEMENT_TYPE) {
			printf("[5.XML �^�O�J�n���m�[�h]\n");
			rxtn = (struct ResXMLTree_node*)bp;
			printf("%s: ���ʎq = 0x%04X\n",
					dump(&rxtn->header.type, 2), rxtn->header.type);
			printf("%s: �{�m�[�h�̃T�C�Y = %d byte\n",
					dump(&rxtn->header.headerSize, 2), rxtn->header.headerSize);
			printf("%s: �{�m�[�h�{(G)�{(I+K)*(H)�̃T�C�Y = %d byte\n",
					dump(&rxtn->header.size, 4), rxtn->header.size);
			printf("%s: ���� XML �t�@�C����� �{�^�O�L�q�J�n�ʒu�̍s�ԍ� = %d\n",
					dump(&rxtn->lineNumber, 4), rxtn->lineNumber);
			printf("%s: �{�m�[�h�ւ̃R�����g�� ������v�[����̗v�f�ԍ� = %d %s\n",
					dump(&rxtn->comment.index, 4), rxtn->comment.index,
					(rxtn->comment.index == 0xFFFFFFFF) ? "(none)" : "");
			putchar('\n');

			bp += rxtn->header.headerSize;

			printf("[5-1.XML �^�O�J�n���g���m�[�h] (G)\n");
			rxtae = (struct ResXMLTree_attrExt*)bp;
			printf("%s: ���̗v�f�̃t�����O��Ԗ��� ������v�[����̗v�f�ԍ� = %d %s\n",
				dump(&rxtae->ns.index, 4), rxtae->ns.index,
				(rxtae->ns.index == 0xFFFFFFFF) ? "(none)" : "");
			printf("%s: ���̃^�O�̗v�f���� ������v�[����̗v�f�ԍ� = %d\n",
				dump(&rxtae->name.index, 4), rxtae->name.index);
			if (rxtae->name.index != 0xFFFFFFFF) {
				printf("%*s-> pool[%02d] = [%s]\n", 14, "",
				rxtae->name.index, ppStrArray[rxtae->name.index]);
			}
			printf("%s: �������J�n�ʒu = 0x%04X (%d)\n",
				dump(&rxtae->attributeStart, 2), 
				rxtae->attributeStart, rxtae->attributeStart);
			printf("%s: �������ꌏ������̃T�C�Y = %d\n",
				dump(&rxtae->attributeSize, 2), rxtae->attributeSize);
			printf("%s: �������̐� = %d (H)\n",
				dump(&rxtae->attributeCount, 2), rxtae->attributeCount);
			printf("%s: �uid�v�����̗v�f�ԍ� = %d (1�I���W��; ���݂��Ȃ���� 0)\n",
				dump(&rxtae->idIndex, 2), rxtae->idIndex);
			printf("%s: �uclass�v�����̗v�f�ԍ� = %d (1�I���W��; ���݂��Ȃ���� 0)\n",
				dump(&rxtae->classIndex, 2), rxtae->classIndex);
			printf("%s: �ustyle�v�����̗v�f�ԍ� = %d (1�I���W��; ���݂��Ȃ���� 0)\n",
				dump(&rxtae->styleIndex, 2), rxtae->styleIndex);
			putchar('\n');

			bp += sizeof(struct ResXMLTree_attrExt);

			if (rxtae->attributeCount == 0) { // ������񖳂�
				continue;
			}

			for (i = 0; i < rxtae->attributeCount; i++) {
				printf("[5-2.�������m�[�h] (I)\n");
				rxta = (struct ResXMLTree_attribute*)bp;

				printf("%s: ���̗v�f�̃t�����O��Ԗ��� ������v�[����̗v�f�ԍ� = %d\n",
					dump(&rxta->ns.index, 4), rxta->ns.index);
				if (rxta->ns.index != 0xFFFFFFFF) {
					printf("%*s-> pool[%02d] = [%s]\n", 14, "",
					rxta->ns.index, ppStrArray[rxta->ns.index]);
				}
				printf("%s: ���̑����̖��O�� ������v�[����̗v�f�ԍ� = %d\n",
					dump(&rxta->name.index, 4), rxta->name.index);
				if (rxta->name.index != 0xFFFFFFFF) {
					printf("%*s-> pool[%02d] = [%s]\n", 14, "",
					rxta->name.index, ppStrArray[rxta->name.index]);
				}
				printf("%s: ���̑����̒l�� ������v�[����̗v�f�ԍ� = %d\n",
					dump(&rxta->rawValue.index, 4), rxta->rawValue.index);
				if (rxta->rawValue.index != 0xFFFFFFFF) {
					printf("%*s-> pool[%02d] = [%s]\n", 14, "",
					rxta->rawValue.index, ppStrArray[rxta->rawValue.index]);
				}
				putchar('\n');

				printf("[5-3.�����l���m�[�h] (K)\n");
				rv = &rxta->typedValue;
				printf("%s: �{�m�[�h�̃o�C�g�� = 0x%04X (%d)\n",
					dump(&rv->size, 2), rv->size, rv->size);
				printf("%s: (pad) = 0x%02X\n",
					dump(&rv->res0, 1), rv->res0);
				printf("%s: �����l�̃^�C�v = 0x%02X\n",
					dump(&rv->dataType, 1), rv->dataType);
				printf("%s: ���̑����̒l = 0x%08X (%d)\n",
					dump(&rv->data, 4), rv->data, rv->data);
				putchar('\n');

				bp += sizeof(struct ResXMLTree_attribute);
			}
		} else if (val16 ==  RES_XML_END_ELEMENT_TYPE) {

			printf("[6.XML �^�O�I�����m�[�h]\n");
			rxtn = (struct ResXMLTree_node*)bp;
			printf("%s: ���ʎq = 0x%04X\n",
					dump(&rxtn->header.type, 2), rxtn->header.type);
			printf("%s: �{�m�[�h�̃T�C�Y = %d byte\n",
					dump(&rxtn->header.headerSize, 2), rxtn->header.headerSize);
			printf("%s: �{�m�[�h�{(N)�̃T�C�Y = %d byte\n",
					dump(&rxtn->header.size, 4), rxtn->header.size);
			printf("%s: ���� XML �t�@�C����� �{�^�O�L�q�J�n�ʒu�̍s�ԍ� = %d\n",
					dump(&rxtn->lineNumber, 4), rxtn->lineNumber);
			printf("%s: �{�m�[�h�ւ̃R�����g�� ������v�[����̗v�f�ԍ� = %d %s\n",
					dump(&rxtn->comment.index, 4), rxtn->comment.index,
					(rxtn->comment.index == 0xFFFFFFFF) ? "(none)" : "");
			putchar('\n');

			bp += rxtn->header.headerSize;

			printf("[6-1.XML �^�O�I�����g���m�[�h] (N)\n");
			rxteee = (struct ResXMLTree_endElementExt*)bp;
			printf("%s: �{�^�O�v�f�̃t�����O��Ԗ��� ������v�[����̗v�f�ԍ� = %d\n",
				dump(&rxteee->ns.index, 4), rxteee->ns.index);
			if (rxteee->ns.index != 0xFFFFFFFF) {
				printf("%*s-> pool[%02d] = [%s]\n", 14, "",
				rxteee->ns.index, ppStrArray[rxteee->ns.index]);
			}
			printf("%s: ���̃^�O�̗v�f���� ������v�[����̗v�f�ԍ� = %d\n",
				dump(&rxteee->name.index, 4), rxteee->name.index);
			if (rxteee->name.index != 0xFFFFFFFF) {
				printf("%*s-> pool[%02d] = [%s]\n", 14, "",
				rxteee->name.index, ppStrArray[rxteee->name.index]);
			}
			putchar('\n');

			bp += sizeof(struct ResXMLTree_endElementExt);

		} else if (val16 == RES_XML_END_NAMESPACE_TYPE) {
			printf("[7.XML �c���[�I�����m�[�h]\n");
			rxtn = (struct ResXMLTree_node*)bp;
			printf("%s: ���ʎq = 0x%04X\n",
					dump(&rxtn->header.type, 2), rxtn->header.type);
			printf("%s: �{�m�[�h�̃T�C�Y = %d byte\n",
					dump(&rxtn->header.headerSize, 2), rxtn->header.headerSize);
			printf("%s: �{�m�[�h�{(O)�̃T�C�Y = %d byte\n",
					dump(&rxtn->header.size, 4), rxtn->header.size);
			printf("%s: ���� XML �t�@�C�����  XML �L�q�I���ʒu�̍s�ԍ� = %d\n",
					dump(&rxtn->lineNumber, 4), rxtn->lineNumber);
			printf("%s: �{�m�[�h�ւ̃R�����g�� ������v�[����̗v�f�ԍ� = %d %s\n",
					dump(&rxtn->comment.index, 4), rxtn->comment.index,
					(rxtn->comment.index == 0xFFFFFFFF) ? "(none)" : "");
			putchar('\n');

			bp += rxtn->header.headerSize;

			printf("[7-1.XML ���O��ԏ��m�[�h] (O)\n");
			rxnse = (struct ResXMLTree_namespaceExt*)bp;
			printf("%s: XML ���O��Ԑړ����� ������v�[����̗v�f�ԍ� = %d\n",
					dump(&rxnse->prefix.index, 4), rxnse->prefix.index);
			if (rxnse->prefix.index != 0xFFFFFFFF) {
				printf("%*s-> pool[%02d] = [%s]\n", 14, "",
					rxnse->prefix.index, ppStrArray[rxnse->prefix.index]);
			}
			printf("%s: XML ���O��� URI �� ������v�[����̗v�f�ԍ� = %d\n",
					dump(&rxnse->uri.index, 4), rxnse->uri.index);
			if (rxnse->uri.index != 0xFFFFFFFF) {
				printf("%*s-> pool[%02d] = [%s]\n", 14, "",
					rxnse->uri.index, ppStrArray[rxnse->uri.index]);
			}
			putchar('\n');
			break;
		}
	}

	// XML �c���[���Č����Ă݂�
	bp = p;
	depth = 0;
	inTag = 0;
	for (;;) {
		val16 = *((uint16_t*)bp);
		if (val16 == RES_XML_START_ELEMENT_TYPE) {

			// �e�v�f�̊J�n�^�O�����
			if (inTag == 1) {
				printf(">\n");
			}
			inTag = 1;

			rxtn = (struct ResXMLTree_node*)bp;
			bp += rxtn->header.headerSize;
			rxtae = (struct ResXMLTree_attrExt*)bp;

			printf("%*s<%s", depth*4, "", ppStrArray[rxtae->name.index]);

			if (depth == 0) {
				printf(" xmlns:%s=\"%s\"", 
					ppStrArray[rxnse->prefix.index], 
					ppStrArray[rxnse->uri.index]);
			}

			bp += sizeof(struct ResXMLTree_attrExt);

			for (i = 0; i < rxtae->attributeCount; i++) {
				rxta = (struct ResXMLTree_attribute*)bp;
				if (rxtae->attributeCount > 2) {
					printf("\n%*s",  (depth+1)*4, "");
				} else {
					printf(" ");
				}
				if (rxta->ns.index == rxnse->uri.index) {
					printf("%s:", ppStrArray[rxnse->prefix.index]);
				}
				printf("%s", ppStrArray[rxta->name.index]);
				if (rxta->rawValue.index != 0xFFFFFFFF) {
					printf("=\"%s\"", ppStrArray[rxta->rawValue.index]);
				} else {
					rv = &rxta->typedValue;
					printf("=\"%s\"", makeAttrValue(rv->dataType, rv->data));
				}

				bp += sizeof(struct ResXMLTree_attribute);
			}
			depth++;
		}
		else if (val16 ==  RES_XML_END_ELEMENT_TYPE) {
			depth--;
			rxtn = (struct ResXMLTree_node*)bp;
			bp += rxtn->header.headerSize;
			rxteee = (struct ResXMLTree_endElementExt*)bp;

			// �^�O�����
			if (inTag == 1) {
				printf(" />\n");
				inTag = 0;
			} else {
				printf("%*s</%s>\n", depth*4, "",
					ppStrArray[rxteee->name.index]);
			}
			bp += sizeof(struct ResXMLTree_endElementExt);
		}
		else if (val16 == RES_XML_END_NAMESPACE_TYPE) {
			break;
		}
	}
	ret = 0;

DONE:
	if (ppStrArray) {
		for (i = 0; i < stringCount; i++) {
			if (ppStrArray[i]) {
				free(ppStrArray[i]);
			}
		}
		free(ppStrArray);
	}
	if (p) {
		free(p);
	}
	if (fp) {
		fclose(fp);
	}
	return ret;
}

int main(int ac, char *av[]) {
	return doParse(ac, av);
}

