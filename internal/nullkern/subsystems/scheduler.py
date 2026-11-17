"""Lightweight distributed scheduling hints for nullkernRT."""

try:
    from ..capability import RIGHT_MANAGE, RIGHT_READ
except ImportError:
    from capability import RIGHT_MANAGE, RIGHT_READ


class NodeInfo:
    __slots__ = ("node_id", "name", "capacity", "load", "features")

    def __init__(self, node_id, name, capacity, load, features=None):
        self.node_id = node_id
        self.name = name
        self.capacity = capacity
        self.load = load
        self.features = dict(features or {})

    def info(self):
        return {
            "node_id": self.node_id,
            "name": self.name,
            "capacity": self.capacity,
            "load": self.load,
            "features": dict(self.features),
        }


class DistributedScheduler:
    """Advertises nodes and scores ability placement without preemption."""

    __slots__ = ("kernel", "nodes", "routes", "placements")

    def __init__(self, kernel=None):
        self.kernel = kernel
        self.nodes = {}
        self.routes = {}
        self.placements = {}

    def _require(self, holder, right):
        if self.kernel is not None and self.kernel.capabilities is not None:
            self.kernel.capabilities.require(holder, "distributed", right)

    def register_node(self, node_id, name, capacity=100, load=0, features=None, holder=-1):
        if not isinstance(node_id, str) or not node_id:
            raise ValueError("node id must be a non-empty string")
        if capacity < 1 or load < 0 or load > 100:
            raise ValueError("capacity must be positive and load must be 0..100")
        self._require(holder, RIGHT_MANAGE)
        node = NodeInfo(node_id, name, capacity, load, features)
        self.nodes[node_id] = node
        return node.info()

    def unregister_node(self, node_id, holder=-1):
        self._require(holder, RIGHT_MANAGE)
        if node_id not in self.nodes:
            raise KeyError("node not found")
        del self.nodes[node_id]
        for ability in list(self.routes.keys()):
            if self.routes[ability] == node_id:
                del self.routes[ability]

    def update_load(self, node_id, load, holder=-1):
        self._require(holder, RIGHT_MANAGE)
        if node_id not in self.nodes:
            raise KeyError("node not found")
        if load < 0 or load > 100:
            raise ValueError("load must be 0..100")
        self.nodes[node_id].load = load
        return self.nodes[node_id].info()

    def advertise(self, node_id, ability_name, holder=-1):
        self._require(holder, RIGHT_MANAGE)
        if node_id not in self.nodes:
            raise KeyError("node not found")
        self.nodes[node_id].features[ability_name] = True
        return self.nodes[node_id].info()

    def set_route(self, ability_name, node_id, holder=-1):
        self._require(holder, RIGHT_MANAGE)
        if node_id not in self.nodes:
            raise KeyError("node not found")
        self.routes[ability_name] = node_id
        self.advertise(node_id, ability_name, holder)
        return {"ability": ability_name, "node_id": node_id}

    def route(self, ability_name, holder=-1):
        self._require(holder, RIGHT_READ)
        return self.routes.get(ability_name)

    def score(self, node_id, ability_name, holder=-1):
        self._require(holder, RIGHT_READ)
        if node_id not in self.nodes:
            raise KeyError("node not found")
        node = self.nodes[node_id]
        value = node.capacity - node.load
        if ability_name in node.features:
            value += 20
        return value

    def rank(self, ability_name, candidates=None, holder=-1):
        self._require(holder, RIGHT_READ)
        node_ids = candidates if candidates is not None else list(self.nodes.keys())
        ranking = []
        for node_id in node_ids:
            if node_id not in self.nodes:
                raise KeyError("node not found")
            ranking.append({
                "node_id": node_id,
                "score": self.score(node_id, ability_name, holder),
                "load": self.nodes[node_id].load,
            })
        for index in range(1, len(ranking)):
            current = ranking[index]
            offset = index - 1
            while offset >= 0:
                previous = ranking[offset]
                if previous["score"] > current["score"]:
                    break
                if previous["score"] == current["score"]:
                    if previous["node_id"] < current["node_id"]:
                        break
                ranking[offset + 1] = previous
                offset -= 1
            ranking[offset + 1] = current
        return ranking

    def place(self, ability_name, candidates=None, holder=-1):
        ranking = self.rank(ability_name, candidates, holder)
        if not ranking:
            return None
        return ranking[0]["node_id"]

    def record_placement(self, ability_name, node_id, holder=-1):
        self._require(holder, RIGHT_MANAGE)
        key = ability_name + "@" + node_id
        self.placements[key] = self.placements.get(key, 0) + 1
        return self.placements[key]

    def info(self, node_id=None, holder=-1):
        self._require(holder, RIGHT_READ)
        if node_id is not None:
            if node_id not in self.nodes:
                raise KeyError("node not found")
            return self.nodes[node_id].info()
        return {
            "nodes": [self.nodes[key].info() for key in self.nodes],
            "routes": dict(self.routes),
            "placements": dict(self.placements),
        }
