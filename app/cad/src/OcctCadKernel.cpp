#include "fulmetry/cad/OcctCadKernel.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pln.hxx>
#include <gp_Quaternion.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <map>

namespace fulmetry::cad {
namespace {

struct Loaded {
    Part part;
    TopoDS_Shape shape;  // local frame
};

Vec3 toVec(const gp_XYZ& p) { return {p.X(), p.Y(), p.Z()}; }
gp_Pnt toPnt(const Vec3& v) { return {v.x, v.y, v.z}; }

Error notFound(const PartId& id) { return Error{"not_found", id.value}; }
Error occtError(const char* what, const Standard_Failure& e) {
    return Error{"occt_error", std::string(what) + ": " + (e.GetMessageString() ? e.GetMessageString() : "")};
}

// Part.placement as an OCCT transform: rotation (unit quaternion w,x,y,z), then translation.
gp_Trsf placementTrsf(const Transform& t) {
    gp_Trsf trsf;
    const Quat& q = t.rotation;
    double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    gp_Quaternion rot;  // identity
    if (n > 1e-12) rot = gp_Quaternion(q.x / n, q.y / n, q.z / n, q.w / n);
    trsf.SetTransformation(rot, gp_Vec(t.position.x, t.position.y, t.position.z));
    return trsf;
}

// True when some vertex of `inner` lies inside a solid of `outer` (catches full containment, which
// BRepExtrema reports as a positive boundary-to-boundary distance).
bool anyVertexInside(const TopoDS_Shape& inner, const TopoDS_Shape& outer) {
    TopExp_Explorer v(inner, TopAbs_VERTEX);
    if (!v.More()) return false;
    gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(v.Current()));
    for (TopExp_Explorer s(outer, TopAbs_SOLID); s.More(); s.Next()) {
        BRepClass3d_SolidClassifier cls(s.Current(), p, 1e-7);
        if (cls.State() == TopAbs_IN) return true;
    }
    return false;
}

void appendEdgePoints(const TopoDS_Edge& edge, Polyline& out) {
    BRepAdaptor_Curve curve(edge);
    GCPnts_TangentialDeflection disc(curve, 0.1 /*rad*/, 0.01 /*mm*/);
    std::vector<Vec3> pts;
    for (int i = 1; i <= disc.NbPoints(); ++i) pts.push_back(toVec(disc.Value(i).XYZ()));
    if (edge.Orientation() == TopAbs_REVERSED) std::reverse(pts.begin(), pts.end());
    for (const Vec3& p : pts) {
        if (!out.empty() && (out.back() - p).length() < 1e-9) continue;
        out.push_back(p);
    }
}

}  // namespace

struct OcctCadKernel::Impl {
    std::map<PartId, Loaded> parts;

    const Loaded* find(const PartId& id) const {
        auto it = parts.find(id);
        return it == parts.end() ? nullptr : &it->second;
    }
};

OcctCadKernel::OcctCadKernel() : impl_(std::make_unique<Impl>()) {}
OcctCadKernel::~OcctCadKernel() = default;

Status OcctCadKernel::loadPart(const Part& part, const std::string& path) {
    try {
        TopoDS_Shape shape;
        if (path.empty()) {
            Vec3 s = part.bounds.size();
            if (!(s.x > 0 && s.y > 0 && s.z > 0)) return Error{"invalid_bounds", part.id.value + ": empty bounds"};
            shape = BRepPrimAPI_MakeBox(toPnt(part.bounds.min), toPnt(part.bounds.max)).Shape();
        } else {
            STEPControl_Reader reader;
            if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) return Error{"io_error", "cannot read STEP " + path};
            if (reader.TransferRoots() <= 0) return Error{"invalid_step", "no shapes in " + path};
            shape = reader.OneShape();
            if (shape.IsNull()) return Error{"invalid_step", "no shapes in " + path};
        }
        impl_->parts[part.id] = Loaded{part, shape};
        return Ok{};
    } catch (const Standard_Failure& e) {
        return occtError("loadPart", e);
    }
}

Result<Mesh> OcctCadKernel::tessellate(const PartId& id, double linearToleranceMm) const {
    const Loaded* l = impl_->find(id);
    if (!l) return notFound(id);
    if (!(linearToleranceMm > 0)) return Error{"invalid_argument", "linear tolerance must be > 0"};
    try {
        TopoDS_Shape shape = l->shape;
        BRepTools::Clean(shape);  // drop any coarser mesh from an earlier call
        BRepMesh_IncrementalMesh mesher(shape, linearToleranceMm, Standard_False, 0.5, Standard_True);
        if (!mesher.IsDone()) return Error{"mesh_failed", id.value};

        TopTools_IndexedMapOfShape faces;
        TopExp::MapShapes(shape, TopAbs_FACE, faces);
        Mesh m;
        for (int fi = 1; fi <= faces.Extent(); ++fi) {
            const TopoDS_Face& face = TopoDS::Face(faces(fi));
            uint32_t faceIndex = uint32_t(m.faces.size());
            m.faces.emplace_back(id.value + "#" + std::to_string(fi - 1));

            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
            if (tri.IsNull()) continue;
            const gp_Trsf& trsf = loc.Transformation();
            const bool reversed = face.Orientation() == TopAbs_REVERSED;
            BRepGProp_Face surf(face);  // Normal() accounts for face orientation
            if (!tri->HasUVNodes() && !tri->HasNormals()) tri->ComputeNormals();

            uint32_t base = uint32_t(m.positions.size() / 3);
            for (int i = 1; i <= tri->NbNodes(); ++i) {
                gp_Pnt p = tri->Node(i).Transformed(trsf);
                gp_Vec n;
                if (tri->HasUVNodes()) {
                    gp_Pnt2d uv = tri->UVNode(i);
                    gp_Pnt sp;
                    surf.Normal(uv.X(), uv.Y(), sp, n);  // already in the face's located frame
                } else {
                    n = gp_Vec(tri->Normal(i)).Transformed(trsf);
                    if (reversed) n.Reverse();
                }
                if (n.Magnitude() > 1e-12) n.Normalize();
                m.positions.insert(m.positions.end(), {float(p.X()), float(p.Y()), float(p.Z())});
                m.normals.insert(m.normals.end(), {float(n.X()), float(n.Y()), float(n.Z())});
            }
            for (int t = 1; t <= tri->NbTriangles(); ++t) {
                int a, b, c;
                tri->Triangle(t).Get(a, b, c);
                if (reversed) std::swap(b, c);
                m.indices.insert(m.indices.end(), {base + uint32_t(a - 1), base + uint32_t(b - 1), base + uint32_t(c - 1)});
                m.triangleFace.push_back(faceIndex);
            }
        }
        return m;
    } catch (const Standard_Failure& e) {
        return occtError("tessellate", e);
    }
}

Result<Aabb> OcctCadKernel::boundingBox(const PartId& id) const {
    const Loaded* l = impl_->find(id);
    if (!l) return notFound(id);
    try {
        Bnd_Box box;
        BRepBndLib::AddOptimal(l->shape, box, /*useTriangulation=*/Standard_False, /*useShapeTolerance=*/Standard_False);
        if (box.IsVoid()) return Error{"empty_shape", id.value};
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        return Aabb{{x0, y0, z0}, {x1, y1, z1}};
    } catch (const Standard_Failure& e) {
        return occtError("boundingBox", e);
    }
}

Result<MassProperties> OcctCadKernel::massProperties(const PartId& id, double densityGPerCm3) const {
    const Loaded* l = impl_->find(id);
    if (!l) return notFound(id);
    try {
        GProp_GProps props;
        BRepGProp::VolumeProperties(l->shape, props);
        double v = props.Mass();  // mm^3
        return MassProperties{v, v / 1000.0 * densityGPerCm3, toVec(props.CentreOfMass().XYZ())};
    } catch (const Standard_Failure& e) {
        return occtError("massProperties", e);
    }
}

Result<double> OcctCadKernel::minDistance(const PartId& a, const PartId& b) const {
    const Loaded* la = impl_->find(a);
    if (!la) return notFound(a);
    const Loaded* lb = impl_->find(b);
    if (!lb) return notFound(b);
    try {
        TopoDS_Shape sa = la->shape.Moved(TopLoc_Location(placementTrsf(la->part.placement)));
        TopoDS_Shape sb = lb->shape.Moved(TopLoc_Location(placementTrsf(lb->part.placement)));
        BRepExtrema_DistShapeShape dist(sa, sb);
        if (!dist.IsDone()) return Error{"distance_failed", a.value + " / " + b.value};
        double d = dist.Value();
        if (d > 0 && (anyVertexInside(sa, sb) || anyVertexInside(sb, sa))) d = 0;  // one inside the other
        return d;
    } catch (const Standard_Failure& e) {
        return occtError("minDistance", e);
    }
}

Result<std::vector<Polyline>> OcctCadKernel::section(const PartId& id, const Plane& plane) const {
    const Loaded* l = impl_->find(id);
    if (!l) return notFound(id);
    if (plane.normal.length() < 1e-12) return Error{"invalid_argument", "plane normal is zero"};
    try {
        gp_Pln pln(toPnt(plane.origin), gp_Dir(plane.normal.x, plane.normal.y, plane.normal.z));
        BRepAlgoAPI_Section cut(l->shape, pln, Standard_False);
        cut.Approximation(Standard_True);
        cut.Build();
        if (!cut.IsDone()) return Error{"section_failed", id.value};

        Handle(TopTools_HSequenceOfShape) edges = new TopTools_HSequenceOfShape;
        for (TopExp_Explorer e(cut.Shape(), TopAbs_EDGE); e.More(); e.Next()) edges->Append(e.Current());
        std::vector<Polyline> out;
        if (edges->IsEmpty()) return out;

        Handle(TopTools_HSequenceOfShape) wires;
        ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges, 1e-6, Standard_False, wires);
        for (int w = 1; w <= wires->Length(); ++w) {
            const TopoDS_Wire& wire = TopoDS::Wire(wires->Value(w));
            Polyline line;
            for (BRepTools_WireExplorer ex(wire); ex.More(); ex.Next()) appendEdgePoints(ex.Current(), line);
            if (line.size() >= 2 && (line.front() - line.back()).length() < 1e-6) line.back() = line.front();
            if (!line.empty()) out.push_back(std::move(line));
        }
        return out;
    } catch (const Standard_Failure& e) {
        return occtError("section", e);
    }
}

Status OcctCadKernel::writeStep(const PartId& id, const std::string& path) const {
    const Loaded* l = impl_->find(id);
    if (!l) return notFound(id);
    try {
        STEPControl_Writer writer;
        if (writer.Transfer(l->shape, STEPControl_AsIs) != IFSelect_RetDone) return Error{"step_write_failed", id.value};
        if (writer.Write(path.c_str()) != IFSelect_RetDone) return Error{"io_error", "cannot write STEP " + path};
        return Ok{};
    } catch (const Standard_Failure& e) {
        return occtError("writeStep", e);
    }
}

}  // namespace fulmetry::cad
