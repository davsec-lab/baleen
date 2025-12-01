#ifndef OBJECT_H
#define OBJECT_H

#include "pin.H"

#include "registry.h"
#include "language.h"

using std::hex;
using std::dec;
using std::ofstream;
using std::map;
using std::endl;

class ObjectTracker {
private:
	PIN_LOCK lock;
	ofstream log;

	// Maps every starting address to its object (name and size).
	Registry objects;

	// Maps the name of every object to its starting address.
	map<string, ADDRINT> starts;

	// Maps the name of every object to its read count.
	map<string, map<Language, UINT64>> reads;

	// Maps the name of every object to its write count.
	map<string, map<Language, UINT64>> writes;

public:
	ObjectTracker();

	VOID RegisterObject(THREADID tid, ADDRINT addr, ADDRINT size, ADDRINT name) {
		PIN_GetLock(&lock, tid + 1);

		// Read object name
		string objectName = "UNKNOWN";

		if (name != 0) {
			char buffer[256];
			PIN_SafeCopy(buffer, (void*)name, sizeof(buffer) - 1);
			buffer[255] = '\0';
			objectName = buffer;
		}

		// Map the address range to the object name
		objects.insert(addr, size, objectName);
		starts[objectName] = addr;

		// Initialize counts
		reads[objectName] = {};
        writes[objectName] = {};

        reads[objectName][Language::C] = 0;
        reads[objectName][Language::RUST] = 0;
		reads[objectName][Language::SHARED] = 0;

        writes[objectName][Language::C] = 0;
        writes[objectName][Language::RUST] = 0;
		writes[objectName][Language::SHARED] = 0;

		log << "[REGISTER OBJECT] Object '" << objectName
			<< "' occupies " << size
			<< " bytes in range [0x" << hex << addr
			<< ", 0x" << addr + size
			<< ")" << dec << endl;

		PIN_ReleaseLock(&lock);
	}

	VOID MoveObject(THREADID tid, ADDRINT oldAddr, ADDRINT newAddr, USIZE size) {
		if (oldAddr == newAddr) return;

		Node *node = objects.remove(oldAddr);

		if (node) {
			log << "[MOVE OBJECT] Object '" << node->name
				<< "' was moved!" << endl;
			
			log << "[MOVE OBJECT] - [0x" << hex << node->start
				<< ", 0x" << node->start + node->size
				<< ") → [0x" << newAddr
				<< ", 0x" << newAddr + size
				<< ")" << dec << endl;
			
			log << "[MOVE OBJECT] - " << node->size
				<< " → " << size
				<< " bytes" << endl;
			
			objects.insert(newAddr, size, node->name);
			starts[node->name] = newAddr;
		}
	}

	VOID RemoveObject(THREADID tid, ADDRINT addr) {
		Node *object = objects.remove(addr);

		if (object) {
			log << "[REMOVE OBJECT] Object '" << object->name
				<< "' is no longer mapped to range [0x" << hex << object->start
				<< ", 0x" << object->start + object->size
				<< ")" << dec << endl;
		
			// TODO: Is this a good way to handle the start address mapping?
			starts[object->name] = 0;
		}
	}

	VOID RecordWrite(THREADID tid, ADDRINT addr, Language lang) {
		PIN_GetLock(&lock, tid + 1);

		auto object = objects.find(addr);

		if (object) {	
			log << "[WRITE] Write to " << hex << addr
			    << " ('" << object->name
				<< "')" << dec
				<< endl;

			writes[object->name][lang]++;
		}

		PIN_ReleaseLock(&lock);
	}

	VOID RecordRead(THREADID tid, ADDRINT addr, Language lang) {
		PIN_GetLock(&lock, tid + 1);

		auto object = objects.find(addr);

		if (object) {	
			log << "[READ] Read from " << hex << addr
			    << " ('" << object->name
				<< "')" << dec << endl;

			reads[object->name][lang]++;
		}

		PIN_ReleaseLock(&lock);
	}

	VOID Report(ofstream& stream) {
		stream << "Name | Reads (Rust) | Reads (C) | Writes (Rust) | Writes (C)" << endl;

		for (const auto& pair : reads) {
			const string& objName = pair.first;

			stream << objName << ", ";

			int rustReads = reads[objName][Language::RUST];
			int cReads = reads[objName][Language::C];

			stream << rustReads << ", " << cReads << ", ";

			int rustWrites = writes[objName][Language::RUST];
			int cWrites = writes[objName][Language::C];

			stream << rustWrites << ", " << cWrites << endl;
		}

		stream << endl;
	}

};

#endif // OBJECT_H