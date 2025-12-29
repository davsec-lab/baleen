// registry.cpp

#include "registry.h"

Registry::Registry() : root(nullptr) {}

void Registry::insert(ADDRINT start, USIZE size, string object, Language lang) {
    // Create the new node
    Node *newNode = new Node;
    newNode->left = nullptr;
    newNode->right = nullptr;
    newNode->name = object;
    newNode->start = start;
    newNode->size = size;
	newNode->lang = lang;
    
    // If tree is empty, set as root
    if (root == nullptr) {
        root = newNode;
        return;
    }
    
    // Standard BST insertion based on starting address
    Node *current = root;
    Node *parent = nullptr;
    
    while (current != nullptr) {
        parent = current;
        
        if (start < current->start) {
            current = current->left;
        } else if (start > current->start) {
            current = current->right;
        } else {
            // Key already exists - could handle this differently
            // For now, we'll just replace the existing node's data
            current->name = object;
            current->size = size;
            delete newNode;
            return;
        }
    }
    
    // Insert the new node
    if (start < parent->start) {
        parent->left = newNode;
    } else {
        parent->right = newNode;
    }
}

// Find the object that contains `addr`.
Node *Registry::find(ADDRINT addr) {
    Node *current = root;
    
    while (current != nullptr) {
        ADDRINT start = current->start;
        ADDRINT end = start + current->size;
        
        if (addr >= start && addr < end) {
            // Address is within [start, start+size)
            return current;
        } else if (addr < start) {
            // Address is before this range, go left
            current = current->left;
        } else {
            // Address lies beyond the current object (>= end), go right
            current = current->right;
        }
    }
    
    // Not found
    return nullptr;
}

// Remove the mapping that uses `key` as its key.
Node *Registry::remove(ADDRINT key) {
    Node *current = root;
    Node *parent = nullptr;
    
    // First, find the node to remove
    while (current != nullptr && current->start != key) {
        parent = current;
        
        if (key < current->start) {
            current = current->left;
        } else {
            current = current->right;
        }
    }
    
    // If not found
    if (current == nullptr) {
        return nullptr;
    }
    
    Node *nodeToReturn = current;
    
    // Case 1: Node has no children
    if (current->left == nullptr && current->right == nullptr) {
        if (parent == nullptr) {
            // Removing root with no children
            root = nullptr;
        } else if (parent->left == current) {
            parent->left = nullptr;
        } else {
            parent->right = nullptr;
        }
    }

    // Case 2: Node has only right child
    else if (current->left == nullptr) {
        if (parent == nullptr) {
            // Removing root
            root = current->right;
        } else if (parent->left == current) {
            parent->left = current->right;
        } else {
            parent->right = current->right;
        }
    }

    // Case 3: Node has only left child
    else if (current->right == nullptr) {
        if (parent == nullptr) {
            // Removing root
            root = current->left;
        } else if (parent->left == current) {
            parent->left = current->left;
        } else {
            parent->right = current->left;
        }
    }

    // Case 4: Node has both children
    else {
        // Find the in-order successor (smallest node in right subtree)
        Node *successorParent = current;
        Node *successor = current->right;
        
        while (successor->left != nullptr) {
            successorParent = successor;
            successor = successor->left;
        }
        
        // Copy successor's data to current node
        current->name = successor->name;
        current->start = successor->start;
        current->size = successor->size;
        
        // Remove the successor node
        if (successorParent == current) {
            successorParent->right = successor->right;
        } else {
            successorParent->left = successor->right;
        }
        
        // Return the successor node (which has the old data)
        nodeToReturn = successor;
    }
    
    return nodeToReturn;
}