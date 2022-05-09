#include "utilities.hpp"

bool operator==(const Edge &lhs, const Edge &rhs)
{
  return (lhs.vA == rhs.vA) && (lhs.vB == rhs.vB);
}

bool operator<(const Edge &lhs, const Edge &rhs)
{
  return (lhs.vA < rhs.vA) || ((lhs.vA == rhs.vA) && (lhs.vB < rhs.vB));
}

namespace std {
template <>
struct hash<Edge> {
  using argument_type = Edge;
  using result_type   = std::size_t;
  result_type operator()(argument_type const &e) const noexcept
  {
    return std::hash<int>{}(e.vA) ^ std::hash<int>{}(e.vB);
  }
};
}; // namespace std

aste::ExecutionContext aste::initializeMPI(int argc, char *argv[])
{
  MPI_Init(&argc, &argv);
  int rank = 0, size = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  return {rank, size};
}

std::vector<int> aste::setupVertexIDs(precice::SolverInterface &interface,
                                      const aste::Mesh &mesh, int meshID)
{
#ifdef ASTE_SET_MESH_BLOCK
  const auto          dimension = interface.getDimensions();
  const auto          nvertices = mesh.positions.size();
  std::vector<double> posData(dimension * nvertices);
  for (unsigned long i = 0; i < nvertices; ++i) {
    const auto &pos = mesh.positions[i];
    assert(pos.size() == dimension);
    std::copy(pos.begin(), pos.end(), &posData[i * dimension]);
  }

  std::vector<int> vertexIDs(nvertices);
  interface.setMeshVertices(meshID, nvertices, posData.data(), vertexIDs.data());
  return vertexIDs;
#else
  std::vector<int> vertexIDs;
  vertexIDs.reserve(mesh.positions.size());
  for (auto const &pos : mesh.positions)
    vertexIDs.push_back(interface.setMeshVertex(meshID, pos.data()));
  return vertexIDs;
#endif
}

EdgeIdMap aste::setupEdgeIDs(precice::SolverInterface &interface, const aste::Mesh &mesh, int meshID, const std::vector<int> &vertexIDs)
{
  VLOG(1) << "Mesh Setup: 2.1) Gather Unique Edges";
  const auto unique_edges{gather_unique_edges(mesh)};

  VLOG(1) << "Mesh Setup: 2.2) Register Edges";
  boost::container::flat_map<Edge, EdgeID> edgeMap;
  edgeMap.reserve(unique_edges.size());

  for (auto const &edge : unique_edges) {
    const auto a = vertexIDs.at(edge[0]);
    const auto b = vertexIDs.at(edge[1]);
    assert(a != b);
    EdgeID eid = interface.setMeshEdge(meshID, a, b);
    edgeMap.emplace_hint(edgeMap.end(), Edge{a, b}, eid);
  }
  return edgeMap;
}

std::vector<int> aste::setupMesh(precice::SolverInterface &interface, const aste::Mesh &mesh, int meshID)
{
  auto tstart = std::chrono::steady_clock::now();

  VLOG(1) << "Mesh Setup: 1) Vertices";
  const auto vertexIDs = setupVertexIDs(interface, mesh, meshID);

  auto tconnectivity = std::chrono::steady_clock::now();
  VLOG(1) << "Mesh Setup: 2) Edges";
  const auto edgeMap = setupEdgeIDs(interface, mesh, meshID, vertexIDs);

  VLOG(1) << "Mesh Setup: 3) Triangles";
  for (auto const &triangle : mesh.triangles) {
    const auto a = vertexIDs[triangle[0]];
    const auto b = vertexIDs[triangle[1]];
    const auto c = vertexIDs[triangle[2]];

    interface.setMeshTriangle(meshID,
                              edgeMap.at(Edge{a, b}),
                              edgeMap.at(Edge{b, c}),
                              edgeMap.at(Edge{c, a}));
  }

  VLOG(1) << "Mesh Setup: 4) Quadrilaterals";
  for (auto const &quadrilateral : mesh.quadrilaterals) {
    const auto a = vertexIDs[quadrilateral[0]];
    const auto b = vertexIDs[quadrilateral[1]];
    const auto c = vertexIDs[quadrilateral[2]];
    const auto d = vertexIDs[quadrilateral[3]];

    interface.setMeshQuad(meshID,
                          edgeMap.at(Edge{a, b}),
                          edgeMap.at(Edge{b, c}),
                          edgeMap.at(Edge{c, d}),
                          edgeMap.at(Edge{d, a}));
  }

  auto tend = std::chrono::steady_clock::now();

  VLOG(1)
      << "Mesh Setup Took "
      << std::chrono::duration_cast<std::chrono::milliseconds>(tend - tstart).count() << "ms ("
      << std::chrono::duration_cast<std::chrono::milliseconds>(tconnectivity - tstart).count() << "ms for vertices, "
      << std::chrono::duration_cast<std::chrono::milliseconds>(tend - tconnectivity).count() << "ms for connectivity)";
  return vertexIDs;
}