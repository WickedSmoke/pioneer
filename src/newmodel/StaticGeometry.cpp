#include "StaticGeometry.h"
#include "NodeVisitor.h"
#include "graphics/Renderer.h"
#include "graphics/Surface.h"
#include "graphics/Material.h"

namespace Newmodel {

StaticGeometry::StaticGeometry() :
	Node()
{
	m_mesh.Reset(new Graphics::StaticMesh(Graphics::TRIANGLES));
}

StaticGeometry::~StaticGeometry()
{
}

void StaticGeometry::Accept(NodeVisitor &nv)
{
	nv.ApplyStaticGeometry(*this);
}

void StaticGeometry::Render(Graphics::Renderer *r, const matrix4x4f &trans)
{
	r->DrawStaticMesh(GetMesh());

	const Aabb &bb = m_boundingBox;
	vector3f min(bb.min.x, bb.min.y, bb.min.z);
	vector3f max(bb.max.x, bb.max.y, bb.max.z);
	vector3f fbl(min.x, min.y, min.z); //front bottom left
	vector3f fbr(max.x, min.y, min.z); //front bottom right
	vector3f ftl(min.x, max.y, min.z); //front top left
	vector3f ftr(max.x, max.y, min.z); //front top right
	vector3f rtl(min.x, max.y, max.z); //rear top left
	vector3f rtr(max.x, max.y, max.z); //rear top right
	vector3f rbl(min.x, min.y, max.z); //rear bottom left
	vector3f rbr(max.x, min.y, max.z); //rear bottom right

#if 0
	std::vector<vector3f> vts;
	//lateral lines
	vts.push_back(fbl);
	vts.push_back(fbr);
	vts.push_back(ftl);
	vts.push_back(ftr);
	vts.push_back(rbl);
	vts.push_back(rbr);
	vts.push_back(rtl);
	vts.push_back(rtr);

	//vertical lines
	vts.push_back(ftl);
	vts.push_back(fbl);
	vts.push_back(ftr);
	vts.push_back(fbr);
	vts.push_back(rtl);
	vts.push_back(rbl);
	vts.push_back(rtr);
	vts.push_back(rbr);

	//directional lines
	vts.push_back(rtl);
	vts.push_back(ftl);
	vts.push_back(rbl);
	vts.push_back(fbl);
	vts.push_back(rtr);
	vts.push_back(ftr);
	vts.push_back(rbr);
	vts.push_back(fbr);

	r->DrawLines(vts.size(), &vts[0], Color(1.f, 0.f, 0.f, 1.f));
#else
	Graphics::VertexArray *vts = new Graphics::VertexArray(Graphics::ATTRIB_POSITION | Graphics::ATTRIB_DIFFUSE);
	Color c(Color::WHITE);
	//vertices
	vts->Add(fbl, c); //0
	vts->Add(fbr, c); //1
	vts->Add(ftl, c); //2
	vts->Add(ftr, c); //3

	vts->Add(rtl, c); //4
	vts->Add(rtr, c); //5
	vts->Add(rbl, c); //6
	vts->Add(rbr, c); //7

	RefCountedPtr<Graphics::Material> mat(new Graphics::Material);
	mat->unlit = true;
	//mat->twoSided = true;
	Graphics::Surface surf(Graphics::TRIANGLES, vts, mat);

	//indices
	std::vector<unsigned short> &ind = surf.GetIndices();
	//Front face
	ind.push_back(3);
	ind.push_back(1);
	ind.push_back(0);

	ind.push_back(0);
	ind.push_back(2);
	ind.push_back(3);

	//Rear face
	ind.push_back(7);
	ind.push_back(5);
	ind.push_back(6);

	ind.push_back(6);
	ind.push_back(5);
	ind.push_back(4);

	//Top face
	ind.push_back(4);
	ind.push_back(5);
	ind.push_back(3);

	ind.push_back(3);
	ind.push_back(2);
	ind.push_back(4);

	//bottom face
	ind.push_back(1);
	ind.push_back(7);
	ind.push_back(6);

	ind.push_back(6);
	ind.push_back(0);
	ind.push_back(1);

	//left face
	ind.push_back(0);
	ind.push_back(6);
	ind.push_back(4);

	ind.push_back(4);
	ind.push_back(2);
	ind.push_back(0);

	//right face
	ind.push_back(5);
	ind.push_back(7);
	ind.push_back(1);

	ind.push_back(1);
	ind.push_back(3);
	ind.push_back(5);

	r->SetWireFrameMode(true);
	r->DrawSurface(&surf);
	r->SetWireFrameMode(false);
#endif
}

}