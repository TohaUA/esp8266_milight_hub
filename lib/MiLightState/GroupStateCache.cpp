#include <GroupStateCache.h>

GroupStateCache::GroupStateCache(const size_t maxSize)
  : maxSize(maxSize)
{ }

GroupStateCache::~GroupStateCache() {
  ListNode<GroupCacheNode*>* cur = cache.getHead();

  while (cur != NULL) {
    delete cur->data;
    cur = cur->next;
  }
}

GroupState* GroupStateCache::get(const BulbId& id) {
  return getInternal(id);
}

GroupState* GroupStateCache::set(const BulbId& id, const GroupState& state) {
  // Check if key exists first, before evicting
  GroupState* cachedState = getInternal(id);

  if (cachedState != NULL) {
    *cachedState = state;
    return cachedState;
  }

  // Key doesn't exist -- make room if needed
  GroupCacheNode* pushedNode = NULL;
  if (cache.size() >= maxSize) {
    pushedNode = cache.pop();
  }

  if (pushedNode == NULL) {
    GroupCacheNode* newNode = new GroupCacheNode(id, state);
    cachedState = &newNode->state;
    cache.unshift(newNode);
  } else {
    pushedNode->id = id;
    pushedNode->state = state;
    cachedState = &pushedNode->state;
    cache.unshift(pushedNode);
  }

  return cachedState;
}

BulbId GroupStateCache::getLru() {
  GroupCacheNode* node = cache.getLast();
  return node->id;
}

bool GroupStateCache::isFull() const {
  return cache.size() >= maxSize;
}

ListNode<GroupCacheNode*>* GroupStateCache::getHead() {
  return cache.getHead();
}

GroupState* GroupStateCache::getInternal(const BulbId& id) {
  ListNode<GroupCacheNode*>* cur = cache.getHead();

  while (cur != NULL) {
    if (cur->data->id == id) {
      GroupState* result = &cur->data->state;
      cache.spliceToFront(cur);
      return result;
    }
    cur = cur->next;
  }

  return NULL;
}
