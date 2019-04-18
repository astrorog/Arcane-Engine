#include "pch.h"
#include "NavigationMesh.h"

#include <ui/NavmeshPane.h>
#include <utils/loaders/ShaderLoader.h>
#include <graphics/mesh/common/Cube.h>

namespace arcane
{
	NavigationMesh::NavigationMesh(Terrain* terrain) : terrain(terrain)
	{
		SetSlopeMesh(NavmeshPane::getNavmeshSlope()); // Checks the angle given for the slope and calculates the cosine of that angle

		regenerationCallback = [&] {OnRegenerateButtonClick(); };
		NavmeshPane::setRegenerationFunctionPtr(regenerationCallback);

		m_GLCache = GLCache::getInstance();
		m_DebugShader = ShaderLoader::loadShader("src/shaders/simple_instanced.vert", "src/shaders/simple_instanced.frag");
		m_DebugVertexModel = new RenderableModel(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, new Model(Cube()), nullptr);
	}

	NavigationMesh::~NavigationMesh()
	{}

	bool NavigationMesh::IsPointOnTriangle(const glm::vec3& point, const TrianglePrim& triangle)
	{
		// Check if point is on the same plane as the triangle
		if (!IsPointOnPlane(point, *triangle.a, *triangle.b, *triangle.c))
			return false;

		// Check if point is inside the triangle or outside within the plane they share
		return SameSideTriangle(point, *triangle.a, *triangle.b, *triangle.c) ||
			SameSideTriangle(point, *triangle.b, *triangle.a, *triangle.c) || 
			SameSideTriangle(point, *triangle.c, *triangle.a, *triangle.b);
	}

	bool NavigationMesh::IsPointOnPlane(const glm::vec3& point, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3)
	{
		glm::vec3 p1p2Crossp1p3 = glm::cross(p2 - p1, p3 - p1); // This is the normal of the plane formed by the 3 points of the plane
		float r = glm::dot(p1, p1p2Crossp1p3); // find the last unknown on the plane equation
		
		return glm::dot(point, p1p2Crossp1p3) + r == 0; // Might need to round this value a little bit
	}

	bool NavigationMesh::SameSideTriangle(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
	{
		glm::vec3 abCrossap = glm::cross(b - a, point - a);
		glm::vec3 abCrossac = glm::cross(b - a, c - a);

		return glm::dot(abCrossap, abCrossac) >= 0;
	}

	void NavigationMesh::SetSlopeMesh(float angle) 
	{
		this->slopeAngle = std::cos(angle);
	}
	
	float NavigationMesh::GetSlopePoints(const glm::vec3& point1, const glm::vec3& point2)
	{
		glm::vec3 difference = point2 - point1; // Getting the difference vectors
		glm::vec3 referenceVec = difference;
		referenceVec.y = point1.y; // Create a vector in the direction of the difference but get straighten it out with reference point's y coordinate
		float dot = glm::dot(glm::normalize(difference), glm::normalize(referenceVec)); // This will give us cos(theta)

		return dot;
	}

	bool NavigationMesh::ObstacleOnPoint(const glm::vec3& point)
	{
		return false;
	}

	bool NavigationMesh::ExistsPathToPoint(const glm::vec3& point, const std::vector<glm::vec3>& terrainPoints)
	{
		// Check if we can go from this point to any of the others around it
		unsigned int columnNumber = terrain->GetVertexCount();
		return false;
	}

	std::vector<TrianglePrim> NavigationMesh::TriangulatePoly(std::vector<std::vector<glm::vec3*>>& polygon)
	{
		std::vector<TrianglePrim> triangles;

		return triangles;
	}

	void NavigationMesh::OnRegenerateButtonClick() {
		std::cout << "Regenerating Nav Mesh" << std::endl;

		SetSlopeMesh(NavmeshPane::getNavmeshSlope());
		GenerateNavigationMesh();
	}

	void NavigationMesh::DrawMesh(ICamera* camera) {
	
	}

	void NavigationMesh::DrawVertices(ICamera* camera) {
		m_GLCache->switchShader(m_DebugShader->getShaderID());

		// Setup model, view, and projection matrix
		glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 110.0f, 0.0f));
		m_DebugShader->setUniformMat4("model", model);
		m_DebugShader->setUniformMat4("view", camera->getViewMatrix());
		m_DebugShader->setUniformMat4("projection", camera->getProjectionMatrix());
		m_DebugVertexModel->draw(m_DebugShader, RenderPassType::ShadowmapPassType);
	}

	void NavigationMesh::GenerateNavigationMesh()
	{
		// Clear the old nav mesh stuff
		m_NavigationPolygon.clear();
		m_TriangulatedPolygon.clear();

		// Filter out the points that we cannot reach
		std::vector<glm::vec3> terrainPoints = terrain->GetPoints();
		int rowNumber = 0;
		int columnCount = terrain->GetVertexCount();

		for (int i = 0; i < terrainPoints.size(); ++i)
		{
			// Check for rows there probably is a better way to do this
			if (i == rowNumber * columnCount)
				++rowNumber;

			// Check if there is an obstacle at this point or whether it is in the list if so forget about it
			if (ObstacleOnPoint(terrainPoints[i]))
				continue; // No obstacles being checked atm

			// Check if any of the points around it can navigate to the point we are currently on 
			bool navigable = false;
			for (int j = -1; j < 2; ++j)
			{
				for (int k = -1; k < 2; ++k)
				{
					// Get the neighboring points
					int index = (i + k) + (j * columnCount);
					if (index < 0 || index >= terrainPoints.size())
						continue;

					glm::vec3* neighborPoint = &terrainPoints[index];

					// Check the slope of the 2 points
					if (GetSlopePoints(terrainPoints[i], *neighborPoint) > COS_30)
					{
						m_NavigationPolygon[rowNumber].push_back(&terrainPoints[i]);
						navigable = true;
						break;
					}
				}

				// If we found something 
				if (navigable)
					break;
			}
		}

		// Triangulate these new points to form a new mesh
		m_TriangulatedPolygon = TriangulatePoly(m_NavigationPolygon);

		// Optimize this mesh for pathfinding by attempting to decrease number of triangles

		// Draw vertices 
		//DrawVertices(m_NavigationPolygon);
		// Draw this new mesh
		//DrawMesh(triangulatedPolygon);
	}
}
