#pragma once
#include "prefab.h"

//forward declarations
class Camera;
class Shader;

using namespace std;

namespace GTR {

	class Prefab;
	class Material;
	
	class RenderCall {
	public:
		Material* material;
		Mesh* mesh;
		BoundingBox world_bounding;
		Matrix44 model;

		float distance_to_camera = 0;

		bool operator > (const RenderCall& str) const
		{
			if (material->alpha_mode == eAlphaMode::BLEND)
				if (str.material->alpha_mode == eAlphaMode::BLEND)
					return (distance_to_camera > str.distance_to_camera);
				else
					return false;
			else
				if (str.material->alpha_mode == eAlphaMode::BLEND)
					return true;
				else
					return (distance_to_camera > str.distance_to_camera);
		}
	};

	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{

	public:
		
		enum ePipeline{
			FORWARD,
			DEFERRED
		};
		enum eRenderShape {
			QUAD,
			GEOMETRY
		};
		
		std::vector<GTR::LightEntity*> lights;
		std::vector<RenderCall> render_calls;
		
		ePipeline pipeline;
		eRenderShape renderShape;
		FBO* gbuffers_fbo;
		FBO* illumination_fbo;
		FBO* ssao_fbo;
		bool show_gbuffers;
		bool show_ssao;
		vector<Vector3> random_points;

		Renderer();
		//add here your functions
		void renderForward(Camera* camera, GTR::Scene* scene);
		void renderDeferred(Camera* camera, GTR::Scene* scene);

		//renders several elements of the scene
		void renderScene(GTR::Scene* scene, Camera* camera);
	
		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterialToGBuffers(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
		void renderMeshWithMaterialAndLighting(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);

		void uploadLightToShaderMultipass(LightEntity* light, Shader* shader);
		void uploadLightToShaderSinglepass(Shader* shader);
	
		void renderFlatMesh(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
		void generateShadowmap(LightEntity* light);
		void showShadowmap(LightEntity* light);
	};

	vector<Vector3> generateSpherePoints(int num, float radius, bool hemi);

	Texture* CubemapFromHDRE(const char* filename);
};