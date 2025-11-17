#ifndef BALEEN_REGISTRY_H
#define BALEEN_REGISTRY_H

#include "pin.H"
#include <memory>
#include <string>

namespace baleen {

struct Node {
  Node *left;
  Node *right;
  std::string object;
  ADDRINT start;
  USIZE size;

  Node(ADDRINT addr, USIZE sz, const std::string &obj)
      : left(nullptr), right(nullptr), object(obj), start(addr), size(sz) {}
};

class Registry {
public:
  Registry();
  ~Registry();

  // Disable copy to avoid shallow copy of BST
  Registry(const Registry &) = delete;
  Registry &operator=(const Registry &) = delete;

  // Insert a memory range mapping
  void insert(ADDRINT start, USIZE size, const std::string &object);

  // Find the object containing the given address
  Node *find(ADDRINT addr);

  // Remove and return the node with the given start address
  Node *remove(ADDRINT key);

  // Check if registry is empty
  bool empty() const { return root == nullptr; }

private:
  Node *root;

  // Helper for destructor
  void deleteTree(Node *node);
};

} // namespace baleen

#endif // BALEEN_REGISTRY_H
