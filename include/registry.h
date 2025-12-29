// registry.h

#ifndef REGISTRY_H
#define REGISTRY_H

#include "pin.H"
#include <string>
#include <types.h>

#include "language.h"

using std::string;

typedef struct Node {
    // Objects with starting addresses less than the key.
    struct Node *left;

    // Objects with starting addresses greater than the key.
    struct Node *right;

    // The name of the object this node represents.
    string name;

    // The address of this object.
    ADDRINT start;

    // The size of the allocated object.
    USIZE size;

	// The language responsible for creating this object.
	Language lang;
} Node;

class Registry {
private:
    Node *root;

public:
    // Construct a new registry.
    Registry();

    // Map address to object.
    void insert(ADDRINT start, USIZE size, string object, Language lang);

    // Find the object that contains `addr`.
    Node *find(ADDRINT addr);

    // Remove the mapping that uses `key` as its key. 
    Node *remove(ADDRINT key);
};

#endif // REGISTRY_H