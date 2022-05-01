#include "renderer.h"

#include "camera.h"
#include "shader.h"
#include "mesh.h"
#include "texture.h"
#include "prefab.h"
#include "material.h"
#include "utils.h"
#include "scene.h"
#include "extra/hdre.h"

#include "fbo.h"
#include "application.h"

#include <algorithm>    // std::sort

using namespace GTR;

void Renderer::renderScene(GTR::Scene* scene, Camera* camera)
{
	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	lights.clear();
	render_calls.clear();

	//render entities
	//first store the lights, they are needed before rendering anything
	for (int i = 0; i < scene->entities.size(); ++i) {
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible)
			continue;
		//is a light!
		if (ent->entity_type == LIGHT)
		{
			LightEntity* light = (GTR::LightEntity*)ent;
			lights.push_back(light);
		}
	}

	//prefabs
	for (int i = 0; i < scene->entities.size(); ++i)
	{
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible)
			continue;

		//is a prefab!
		if (ent->entity_type == PREFAB)
		{
			PrefabEntity* pent = (GTR::PrefabEntity*)ent;
			if (pent->prefab) {
				renderPrefab(ent->model, pent->prefab, camera);
			}
		}
	}

	//shadowmaps
	for (int i = 0; i < lights.size(); i++) {
		LightEntity* light = lights[i];
		if (light->cast_shadows)
			generateShadowmap(light);
	}

	//rendercalls
	std::sort(render_calls.begin(), render_calls.end(), std::greater<RenderCall>());

	for (std::vector<GTR::RenderCall>::iterator rc = render_calls.begin(); rc != render_calls.end(); ++rc) {
		if (camera->testBoxInFrustum(rc->world_bounding.center, rc->world_bounding.halfsize))
			renderMeshWithMaterial(rc->model, rc->mesh, rc->material, camera);
	}
	
	//glViewport(Application::instance->window_width - 256, 0, 256, 256);
	//showShadowmap(lights[0]);
	//glViewport( 0, 0, Application::instance->window_width, Application::instance->window_height);

}

void GTR::Renderer::showShadowmap(LightEntity* light){
	//QUITAR
	Shader* shader = Shader::getDefaultShader("depth");
	shader->enable();
	shader->setUniform("u_camera_nearfar", Vector2(light->light_camera->near_plane, light->light_camera->far_plane));
	light->shadowmap->toViewport(shader);
}

//renders all the prefab
void Renderer::renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera)
{
	assert(prefab && "PREFAB IS NULL");
	//assign the model to the root node
	renderNode(model, &prefab->root, camera);
}

//renders a node of the prefab and its children
void Renderer::renderNode(const Matrix44& prefab_model, GTR::Node* node, Camera* camera)
{
	if (!node->visible)
		return;

	//compute global matrix
	Matrix44 node_model = node->getGlobalMatrix(true) * prefab_model;

	//does this node have a mesh? then we must render it
	if (node->mesh && node->material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		BoundingBox world_bounding = transformBoundingBox(node_model,node->mesh->box);
		
		//if bounding box is inside the camera frustum then the object is probably visible
		//if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize) )
		{	//CAMBIAR A QUE DEPENDA DE LA LIGHT CAMERA TAMBIEN
			//render node mesh
			//renderMeshWithMaterial( node_model, node->mesh, node->material, camera );
			//node->mesh->renderBounding(node_model, true);
			RenderCall rc;
			rc.material = node->material;
			rc.model = node_model;
			rc.mesh = node->mesh;
			rc.world_bounding = world_bounding;
			render_calls.push_back(rc);
		}
	}

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		renderNode(prefab_model, node->children[i], camera);
}

//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material )
		return;
    assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;
	Texture* texture = NULL;
	GTR::Scene* scene = GTR::Scene::instance;

	texture = material->color_texture.texture;
	//texture = material->emissive_texture;
	//texture = material->metallic_roughness_texture;
	//texture = material->normal_texture;
	//texture = material->occlusion_texture;
	if (texture == NULL)
		texture = Texture::getWhiteTexture(); //a 1x1 white texture

	//select the blending
	if (material->alpha_mode == GTR::eAlphaMode::BLEND)	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		glDisable(GL_BLEND);
	}

	//select if render both sides of the triangles
	if(material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
    assert(glGetError() == GL_NO_ERROR);

	//chose a shader
	if (scene->multi_pass) shader = Shader::Get("multilight");
	else shader = Shader::Get("singlelight");

    assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model );
	float t = getTime();
	shader->setUniform("u_time", t );

	shader->setUniform("u_color", material->color);
	if(texture)
		shader->setUniform("u_texture", texture, 0);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	//Light
	shader->setUniform("u_ambient_light", scene->ambient_light);
	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	int num_lights = lights.size();

	//Multipass
	if (!num_lights) {
		shader->setUniform("u_light_color", Vector3());
		mesh->render(GL_TRIANGLES);
	}
	else if (scene->multi_pass) {
		for (int i = 0; i < num_lights; ++i) {
			if (i == 0) {
				glDisable(GL_BLEND);
				if (material->alpha_mode == GTR::eAlphaMode::BLEND)
				{
					glEnable(GL_BLEND);
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				}
				else {
					glDisable(GL_BLEND);
				}
			}
			else {
				glBlendFunc(GL_SRC_ALPHA, GL_ONE); //MIRAR

				glEnable(GL_BLEND);
			}
			LightEntity* light = lights[i];
			shader->setUniform("u_light_color", light->color * light->intensity);
			shader->setUniform("u_light_position", light->model * Vector3());
			shader->setUniform("u_light_max_distance", light->max_distance);
			shader->setUniform("u_light_type", (int)light->light_type);

			shader->setUniform("u_light_direction", light->model.rotateVector(Vector3(0, 0, -1)));

			shader->setUniform("u_light_exp", light->cone_exp);
			shader->setUniform("u_light_cosine_cutoff", (float)cos(light->cone_angle * DEG2RAD));

			if (light->shadowmap) {
				shader->setUniform("u_light_cast_shadows", light->cast_shadows);
				shader->setUniform("u_light_shadowmap", light->shadowmap, 8);
				shader->setUniform("u_shadow_viewproj", light->light_camera->viewprojection_matrix);
				shader->setUniform("u_light_shadowbias", light->shadow_bias);
			}
			else {
				shader->setUniform("u_light_cast_shadows", 0);
			}

			//do the draw call that renders the mesh into the screen
			mesh->render(GL_TRIANGLES);

			shader->setUniform("u_ambient_light", Vector3());
		}
	}
	//Singlepass
	else {
		const int MAX_LIGHTS = 5;
		Vector3 light_position[MAX_LIGHTS];
		Vector3 light_color[MAX_LIGHTS];
		int light_type[MAX_LIGHTS];
		float light_max_distance[MAX_LIGHTS];
		Vector3 light_direction[MAX_LIGHTS];
		float light_exp[MAX_LIGHTS];
		float light_cosine_cutoff[MAX_LIGHTS];

		//int light_cast_shadows[MAX_LIGHTS];
		//Texture* light_shadowmap[MAX_LIGHTS];
		//Matrix44 shadow_viewproj[MAX_LIGHTS];
		//float light_shadowbias[MAX_LIGHTS];

		Matrix44 empty;

		for (int i = 0; i < MAX_LIGHTS; ++i) {
			if (i < num_lights) {
				LightEntity* light = lights[i];
				light_position[i] = light->model * Vector3();
				light_color[i] = light->color * light->intensity;
				light_type[i] = light->light_type;
				light_max_distance[i] = light->max_distance;
				light_direction[i] = light->model.rotateVector(Vector3(0, 0, -1));
				light_exp[i] = light->cone_exp;
				light_cosine_cutoff[i] = cos(light->cone_angle * DEG2RAD);

				
				//light_cast_shadows[i] = (int)light->cast_shadows;
				//if (light->cast_shadows && light->light_camera) {
				//	light_shadowmap[i] = light->shadowmap;
				//	shadow_viewproj[i] = light->light_camera->viewprojection_matrix;
				//	light_shadowbias[i] = light->shadow_bias;
				//}
				//else {
				//	light_shadowmap[i] = NULL;
				//	shadow_viewproj[i] = empty;//AQUI
				//	light_shadowbias[i] = NULL;
				//}
			}
		}
		shader->setUniform3Array("u_light_color", (float*)&light_color, num_lights);
		shader->setUniform3Array("u_light_position", (float*)&light_position, num_lights);
		shader->setUniform3Array("u_light_direction", (float*)&light_direction, num_lights);
		shader->setUniform("u_num_lights", num_lights);

		shader->setUniform1Array("u_light_max_distance", (float*)&light_max_distance, num_lights);
		shader->setUniform1Array("u_light_type", (int*)&light_type, num_lights);
		shader->setUniform1Array("u_light_exp", (float*)&light_exp, num_lights);
		shader->setUniform1Array("u_light_cosine_cutoff", (float*)&light_cosine_cutoff, num_lights);

		//shader->setUniform1Array("u_light_cast_shadows", (int*)&light_cast_shadows, num_lights);
		//shader->setUniform1Array("u_light_shadowmap", (float*)&light_shadowmap, num_lights);
		//shader->setUniform1Array("u_shadow_viewproj", (float*)&shadow_viewproj, num_lights);
		//shader->setUniform1Array("u_light_shadowbias", (float*)&light_shadowbias, num_lights);
		//do the draw call that renders the mesh into the screen
		mesh->render(GL_TRIANGLES);

		shader->setUniform("u_ambient_light", Vector3());
	}
	
	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glDepthFunc(GL_LESS);
}

Texture* GTR::CubemapFromHDRE(const char* filename)
{
	HDRE* hdre = HDRE::Get(filename);
	if (!hdre)
		return NULL;

	Texture* texture = new Texture();
	if (hdre->getFacesf(0))
	{
		texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesf(0),
			hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_FLOAT);
		for (int i = 1; i < hdre->levels; ++i)
			texture->uploadCubemap(texture->format, texture->type, false,
				(Uint8**)hdre->getFacesf(i), GL_RGBA32F, i);
	}
	else
		if (hdre->getFacesh(0))
		{
			texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesh(0),
				hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_HALF_FLOAT);
			for (int i = 1; i < hdre->levels; ++i)
				texture->uploadCubemap(texture->format, texture->type, false,
					(Uint8**)hdre->getFacesh(i), GL_RGBA16F, i);
		}
	return texture;
}

void Renderer::renderFlatMesh(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;
	GTR::Scene* scene = GTR::Scene::instance;

	//select the blending
	if (material->alpha_mode == GTR::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		glDisable(GL_BLEND);
	}

	//select if render both sides of the triangles
	if (material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	//chose a shader
	shader = Shader::Get("flat");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_model", model);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);

	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();
}

void GTR::Renderer::generateShadowmap(LightEntity* light){
	if (light->light_type != eLightType::SPOT)
		return;
	if (!light->cast_shadows) {
		if (light->fbo) {
			delete light->fbo;
			light->fbo = NULL;
			light->shadowmap = NULL;
		}
		return;
	}

	if (!light->fbo) {
		light->fbo = new FBO();
		light->fbo->setDepthOnly(1024, 1024);
		light->shadowmap = light->fbo->depth_texture;
	}

	if (!light->light_camera)
		light->light_camera = new Camera();

	light->fbo->bind();

	Camera* light_camera = light->light_camera;
	Camera* view_camera = Camera::current;
	light_camera->setPerspective(light->cone_angle, 1.0, 0.1, light->max_distance);
	light_camera->lookAt(light->model.getTranslation(), light->model * Vector3(0, 0, -1), light->model.rotateVector(Vector3(0, 1, 0)));
	light_camera->enable();

	glClear(GL_DEPTH_BUFFER_BIT);

	for (int i = 0; i < render_calls.size(); i++) {
		RenderCall& rc = render_calls[i];
		if (rc.material->alpha_mode == eAlphaMode::BLEND)
			continue;
		if (light_camera->testBoxInFrustum(rc.world_bounding.center, rc.world_bounding.halfsize))
			renderFlatMesh(rc.model, rc.mesh, rc.material, light_camera);
	}

	light->fbo->unbind();
	view_camera->enable();
}