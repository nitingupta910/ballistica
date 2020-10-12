// Released under the MIT License. See LICENSE for details.

#ifndef BALLISTICA_SCENE_NODE_NULL_NODE_H_
#define BALLISTICA_SCENE_NODE_NULL_NODE_H_

#include "ballistica/scene/node/node.h"

// empty node type - just used as a building block
namespace ballistica {

class Scene;

// An empty node.
class NullNode : public Node {
 public:
  static auto InitType() -> NodeType*;
  explicit NullNode(Scene* scene);
};

}  // namespace ballistica

#endif  // BALLISTICA_SCENE_NODE_NULL_NODE_H_
