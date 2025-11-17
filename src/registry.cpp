#include "../include/registry.h"

namespace baleen {

Registry::Registry() : root(nullptr) {}

Registry::~Registry() { deleteTree(root); }

void Registry::deleteTree(Node *node) {
  if (node == nullptr)
    return;

  deleteTree(node->left);
  deleteTree(node->right);
  delete node;
}

void Registry::insert(ADDRINT start, USIZE size, const std::string &object) {
  Node *newNode = new Node(start, size, object);

  if (root == nullptr) {
    root = newNode;
    return;
  }

  Node *current = root;
  Node *parent = nullptr;

  while (current != nullptr) {
    parent = current;

    if (start < current->start) {
      current = current->left;
    } else if (start > current->start) {
      current = current->right;
    } else {
      // Key already exists - update existing node
      current->object = object;
      current->size = size;
      delete newNode;
      return;
    }
  }

  // Insert new node
  if (start < parent->start) {
    parent->left = newNode;
  } else {
    parent->right = newNode;
  }
}

Node *Registry::find(ADDRINT addr) {
  Node *current = root;

  while (current != nullptr) {
    ADDRINT start = current->start;
    ADDRINT end = start + current->size;

    if (addr >= start && addr < end) {
      return current;
    } else if (addr < start) {
      current = current->left;
    } else {
      current = current->right;
    }
  }

  return nullptr;
}

Node *Registry::remove(ADDRINT key) {
  Node *current = root;
  Node *parent = nullptr;

  // Find the node to remove
  while (current != nullptr && current->start != key) {
    parent = current;

    if (key < current->start) {
      current = current->left;
    } else {
      current = current->right;
    }
  }

  if (current == nullptr) {
    return nullptr;
  }

  Node *nodeToReturn = current;

  // (Case 1) No children
  if (current->left == nullptr && current->right == nullptr) {
    if (parent == nullptr) {
      root = nullptr;
    } else if (parent->left == current) {
      parent->left = nullptr;
    } else {
      parent->right = nullptr;
    }
  }
  // (Case 2) Only right child
  else if (current->left == nullptr) {
    if (parent == nullptr) {
      root = current->right;
    } else if (parent->left == current) {
      parent->left = current->right;
    } else {
      parent->right = current->right;
    }
  }
  // (Case 3) Only left child
  else if (current->right == nullptr) {
    if (parent == nullptr) {
      root = current->left;
    } else if (parent->left == current) {
      parent->left = current->left;
    } else {
      parent->right = current->left;
    }
  }
  // (Case 4) Both children
  else {
    Node *successorParent = current;
    Node *successor = current->right;

    while (successor->left != nullptr) {
      successorParent = successor;
      successor = successor->left;
    }

    // Copy successor data
    current->object = successor->object;
    current->start = successor->start;
    current->size = successor->size;

    // Remove successor
    if (successorParent == current) {
      successorParent->right = successor->right;
    } else {
      successorParent->left = successor->right;
    }

    nodeToReturn = successor;
  }

  return nodeToReturn;
}

} // namespace baleen
