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

#include <random>
#include "framework.h"

#include "application.h"
#include "sphericalharmonics.h"

#include <algorithm>



using namespace GTR;



//this->color_buffer = new Texture(width, height, GL_RGB, GL_HALF_FLOAT); // 2 componentes
//this->fbo.setTexture(color_buffer); // para evitar de hacerlo en cada frame 

GTR::Renderer::Renderer()
{
	int width = Application::instance->window_width;
	int height = Application::instance->window_height;

	//Flags
	this->render_mode = GTR::eRenderMode::MULTI;
	this->pipeline_mode = GTR::ePipelineMode::DEFERRED;
	this->rendering_shadowmap = false;
	this->update_shadowmaps = true;
	this->show_shadowmap = false;
	this->show_gbuffers = false;
	this->show_ao = false;
	this->show_ao_deferred = false;
    this->show_volumetric_rendering = true;
    this->updateIrradiance = true; // It should be set to true if there is a movement of an object to update the irradiance
    this->show_irradiance = true;
    this->show_probes = false;
	
	//FrameBufferObject
	
	this->gbuffers_fbo.create(width, height, 3, GL_RGBA, GL_UNSIGNED_BYTE, true); 
	this->illumination_fbo.create(width, height, 1, GL_RGB, GL_FLOAT, true);
	this->irr_fbo.create(64, 64, 1, GL_RGB, GL_FLOAT);//Creamos la textura 63x63pixeles, no necesitamos alpha, interesa que sea float para mantener reso. ilum.
	
	
	//Textures
	this->ao_buffer = new Texture(width * 0.5, height * 0.5, GL_RED, GL_UNSIGNED_BYTE);
	


	//------probes:

	// a place to store the probes
	//memset(&probe, 0, sizeof(probe)); //reserva la memoria para el probe
	//probe.sh.coeffs[0].set(1.0, 0.0, 0.0);
	//probe.pos.set(10, 10, 10);
	


	
}

// render in texture
void Renderer::render2FBO(GTR::Scene* scene, Camera* camera) {
	
	
	renderScene(scene, camera);

	if (this->show_ao && ao_buffer)
		ao_buffer->toViewport();

	int width = Application::instance->window_width;
	int height = Application::instance->window_height;

	if (this->show_gbuffers && gbuffers_fbo.fbo_id != 0)
		showGbuffers(width, height, camera); 

	
}


struct sortRC {
	inline bool operator()(RenderCall& a, RenderCall& b) const {
		
		//sort the rc through distance more to less if the material is the type BLEND
		if ((a.material->alpha_mode == GTR::eAlphaMode::BLEND) && (b.material->alpha_mode == GTR::eAlphaMode::BLEND))
			return a.dist2camera > b.dist2camera;
		//if the material is the type OPAQUE OR OTHERS, we sort it less to more
		else if ((a.material->alpha_mode != GTR::eAlphaMode::BLEND) && (b.material->alpha_mode != GTR::eAlphaMode::BLEND))
			return a.dist2camera < b.dist2camera;
		//if don't comply above conditions, we still sort it by the type of the material. If is type BLEND, sort it at the end 
		return a.material->alpha_mode < b.material->alpha_mode;

	}
};


void Renderer::renderScene(GTR::Scene* scene, Camera* camera)
{
	//std::vector<RenderCall> rc_data_list;
		
	collectRenderCalls(scene, camera, this->rc_data_list);
	//sort each rcs after rendering one pass of all the scene
	//std::sort(this->rc_data_list.begin(), this->rc_data_list.end(), sortRC());

	/*
	int i = 0;
	std::vector<RenderCall> rc_data_no_alpha;
	std::vector<RenderCall> rc_data_alpha;
	for (i; i < rc_data_list.size(); i++) {
		if (rc_data_list[i].material->alpha_mode == GTR::eAlphaMode::BLEND)
			break;
		rc_data_no_alpha.push_back(rc_data_list[i]);
	}
	for (i; i < rc_data_list.size(); i++) {
		rc_data_alpha.push_back(rc_data_list[i]);
	}*/
	
	if (update_shadowmaps)
		createShadowmap(scene, camera);

	
		

	if (pipeline_mode == FORWARD) {
		renderForward(scene, this->rc_data_list, camera, true);
	}

	else if (pipeline_mode == DEFERRED) {
		renderDeferred(scene, this->rc_data_list, camera);
		//glClearColor(0, 0, 0, 0);
		/*
		illumination_fbo.bind(); //TIENE DEPTH
		this->gbuffers_fbo.depth_texture->copyTo(NULL);
		glEnable(GL_DEPTH_TEST);
		renderForward(scene, rc_data_alpha, camera, false);
		glDisable(GL_DEPTH_TEST);

		illumination_fbo.unbind();
		*/
		
		illumination_fbo.color_textures[0]->toViewport(); //me he quedado aqui, no se q le pasa la luz spotlight...
		//renderProbe(probe.pos, 2.0, probe.sh.coeffs[0].v); //coje la direccion del primer elemento, y los demas vienen despues

		//applyfinalHDR();
		
		//gbuffers_fbo.color_textures[0]->toViewport(Shader::Get("showAlpha"));        

	}



    
	/*
   if (show_irradiance){
        updateIrradianceCache(scene);
        //renderProbesGrid(); // to render on the screen to visualize it
        Mesh* quad = Mesh::getQuad();
        Shader* shader = Shader::Get("irradiance_sh");
        uploadIrradianceUniforms(shader, camera);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        quad->render(GL_TRIANGLES);
        glDisable(GL_BLEND);
        glEnableGL_DEPTH_TEST);
    }
*/

	
	/*
	if (this->update_shadowmaps) {

		LightEntity* light;
		for (int i = 0; i < this->light_entities.size(); i++)
		{
			light = light_entities[i];
			if (!light->cast_shadows) // si no emite, continua
			{
				continue;
			}
			//just pass the lights that need to cast shadows
			//updateShadowmap(scene, light, camera);
		}
	}

	

	if (rendering_shadowmap) {
		return;
	}
	createShadowmap(scene, camera);
	*/

	if(show_shadowmap)
		showShadowmap(camera);
}


//collect all RC
void Renderer::collectRenderCalls(GTR::Scene* scene, Camera* camera, std::vector<RenderCall>& rc_vector) {

	//clear data_lists
	//this->rc_data_list.resize(0);
    rc_vector.resize(0);
	this->light_entities.resize(0);
	this->decal_entities.resize(0);

	//render entities
	for (int i = 0; i < scene->entities.size(); ++i)
	{
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible)
			continue;

		//is a prefab!
		if (ent->entity_type == PREFAB)
		{
			PrefabEntity* pent = (GTR::PrefabEntity*)ent; //down-cast 
			if (pent->prefab)

				getRCsfromPrefab(ent->model, pent->prefab, camera, rc_vector);

		}

		//is a light!
		else if (ent->entity_type == LIGHT)
		{
			LightEntity* lig = (GTR::LightEntity*)ent; //down-cast 

			// if light is not in the fustrum of the camera, we don't add it to the conteiner
			// directional light affect all the places so we will add it
			Vector3 light_pos = lig->model.getTranslation();
			if (camera != NULL && (lig->light_type != eLightType::DIRECTIONAL && camera->testSphereInFrustum(light_pos, lig->max_dist) == CLIP_OUTSIDE))
				continue;

			this->light_entities.push_back(lig);

		}

		else if (ent->entity_type == DECAL) 
		{
			DecalEntity* decal = (GTR::DecalEntity*)ent;
			this->decal_entities.push_back(decal);

		}

	}

	if (camera) {
		//sort each rcs after rendering one pass of all the scene
		std::sort(this->rc_data_list.begin(), this->rc_data_list.end(), sortRC());

	}
	
}


void GTR::Renderer::renderForward(GTR::Scene* scene, std::vector <RenderCall>& rendercalls, Camera* camera, bool apply_clear)
{

	if (apply_clear) {
		//set the clear color (the background color)
		glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
		// Clear the color and the depth buffer
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		checkGLErrors();

	}

	//------------------render skybox 
	//if (scene->environment)
	//	renderSkybox(scene->environment, camera);

	//render RenderCalls through reference 
	for (int i = 0; i < rendercalls.size(); i++)
	{
		RenderCall& rc = rendercalls[i];
		renderMeshWithMaterial(this->render_mode, rc.model, rc.mesh, rc.material, camera);
	}


}

void GTR::Renderer::createGbuffers(int width, int height, std::vector <RenderCall>& rendercalls, Camera* camera) {
	

	//start rendeing inside the gbuffers
	gbuffers_fbo.bind();

	// if we want to clear all in once
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	/* //if clear each GB independently
	//Now we clear in several passes, so we can control the clear color independently for every gbuffer
	//disable all but the GB0 (and the depth)
	gbuffers_fbo.enableSingleBuffer(0);

	//clear the 1� GB with the color (and depth)
	glClearColor( 0.1 , 0.1 , 0.1 , 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	//now enable the 2� GB and clear. This time we haven't clear the GL_COLOR
	gbuffers_fbo.enableSingleBuffer(1);
	glClearColor( 0, 0, 0, 1.0);
	glClear(GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	// clear 3� GB

	// clear 4� GB

	//enable all buffers back
	gbuffers_fbo.enableAllBuffers();

	*/

	for (int i = 0; i < rendercalls.size(); i++)//render all
	{
		RenderCall& rc = rendercalls[i]; 
		renderMeshWithMaterial(eRenderMode::GBUFFERS, rc.model, rc.mesh, rc.material, camera); //always in gbuffer mode
	}
	 
	

	//stop rendering to the gbuffers
	gbuffers_fbo.unbind();

}

void GTR::Renderer::showGbuffers(int width, int height, Camera* camera) {


	//to plot just alpha component
	//gbuffers_fbo.color_textures[0]->toViewport(Shader::Get("showAlpha"));
	//if (!this->show_gbuffers)
	//	return;

	FBO* fbo = &gbuffers_fbo;

	//GB0 color
	glViewport(0, 0, width * 0.5, height * 0.5); //set area of the screen and render fullscreen quad
	fbo->color_textures[0]->toViewport();

	//GB1 normal
	glViewport(width * 0.5, 0, width * 0.5, height * 0.5);
	fbo->color_textures[1]->toViewport();

	//GB2 material. properties
	glViewport(width * 0.5, height * 0.5, width * 0.5, height * 0.5);
	fbo->color_textures[2]->toViewport();

	//GB3 depth_buffer
	glViewport(0, height * 0.5, width * 0.5, height * 0.5);
	//need to pass a linear with shader depth, to be able to see. No se ve-> pq necesita parm
	// para linealizar necesito el near and far de la camara.
	Shader* depth_sh = Shader::Get("depth");
	depth_sh->enable();
	depth_sh->setUniform("u_camera_nearfar", Vector2(camera->near_plane, camera->far_plane));
	//depth_sh->disable();
	fbo->depth_texture->toViewport(depth_sh);

	//Volver a poner el tama�o de VPort. 0,0 en una textura esta abajo iz!
	glViewport(0, 0, width, height);
	
	
}


void GTR::Renderer::createDecalsFBO(int width, int height, Camera* camera)
{
	//After remderimng the scene of gbuffers, we will do some decals, and we will clone it to decal_fbo
	//we need have depth cloned, as FBOs do not allow to read from the shader the current binded buffers(gbuffer).


	if (this->decals_fbo.fbo_id == 0) {
		// we need to clone the fbo of gbuffer
		this->decals_fbo.create(width, height, 3, GL_RGBA, GL_UNSIGNED_BYTE);
	}

	gbuffers_fbo.color_textures[GTR::eChannels::ALBEDO]->copyTo(decals_fbo.color_textures[0]);
	this->gbuffers_fbo.color_textures[GTR::eChannels::NORMAL]->copyTo(decals_fbo.color_textures[1]);
	this->gbuffers_fbo.color_textures[GTR::eChannels::EMISSIVE]->copyTo(decals_fbo.color_textures[2]);

	decals_fbo.bind();
	this->gbuffers_fbo.depth_texture->copyTo(NULL);
	renderDecals(camera);
	decals_fbo.unbind();

	decals_fbo.color_textures[GTR::eChannels::ALBEDO]->copyTo(gbuffers_fbo.color_textures[0]);
	this->decals_fbo.color_textures[GTR::eChannels::NORMAL]->copyTo(gbuffers_fbo.color_textures[1]);
	this->decals_fbo.color_textures[GTR::eChannels::EMISSIVE]->copyTo(gbuffers_fbo.color_textures[2]);

}


void GTR::Renderer::renderDeferred(GTR::Scene* scene, std::vector <RenderCall>& rendercalls, Camera* camera)
{
	int width = Application::instance->window_width;
	int height = Application::instance->window_height;

	createGbuffers(width, height, rendercalls, camera);
	
	createDecalsFBO(width, height, camera);
		
	ssao.applyEffect(gbuffers_fbo.depth_texture, gbuffers_fbo.color_textures[GTR::eChannels::NORMAL], camera, ao_buffer);

	//---------Ilumination_Pass--------------

	//now if we enable depth_test during the illumination pass it will take into account the scene depth buffer
	illumination_fbo.bind();
    
    // if we want to clear all in once
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    checkGLErrors();
    
    //clone the depth buffer content to the other depth buffer so they contain the same
    //therefore, we can have the contain in the scene deth and block writing it to avoid any modification while render
    this->gbuffers_fbo.depth_texture->copyTo(NULL);
	glDepthMask(false); //now we can block writing to it
    
	Mesh* quad = Mesh::getQuad(); 
	Shader* shader = Shader::Get("deferred");
	shader->enable();
	shader->setTexture("u_color_texture", gbuffers_fbo.color_textures[0], GTR::eChannels::ALBEDO);
	shader->setTexture("u_normal_texture", gbuffers_fbo.color_textures[1], GTR::eChannels::NORMAL);
	shader->setTexture("u_extra_texture", gbuffers_fbo.color_textures[2], GTR::eChannels::EMISSIVE);
	shader->setTexture("u_depth_texture", gbuffers_fbo.depth_texture, GTR::eChannels::DEPTH);
	shader->setUniform("u_ambient_light", (scene->ambient_light)); //degamma
	shader->setTexture("u_ao_texture", ao_buffer, GTR::eChannels::OCCLUSION);
	
	shader->setUniform("u_ao_show", show_ao_deferred);
	shader->setUniform("u_camera_position", camera->eye);


	Matrix44 inv_vp = camera->viewprojection_matrix;
	inv_vp.inverse();
	Vector2 iRes = Vector2(1.0 / (float)width, 1.0 / (float)height);
	shader->setUniform("u_inverse_viewprojection", inv_vp);
	shader->setUniform("u_iRes", iRes );

	
	//disable depth and blend
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	LightEntity* light;
	for (int i = 0; i < this->light_entities.size(); i++)
	{
		light = this->light_entities[i];
		//we assume that there is always at least one directional ///luego si da tiempo corregir para el caso de no directional light
		if (light->light_type == DIRECTIONAL) {
			light->uploadToShader(shader);

			quad->render(GL_TRIANGLES);

			//in case there are more than one directional light:

			shader->setUniform("u_ambient_light", Vector3(0, 0, 0));
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE);
			//light_entities.erase(i) 

		}
	}

	//---------Using geometry--------------
	 
	//we can use a sphere mesh for point lights
	Mesh* sphere = Mesh::Get("data/meshes/sphere.obj", false, false);
	
    // Activate CULL FACE to render only one time every pixel
	glEnable(GL_CULL_FACE);
    // To control the case when the camera is inside the sphere (in which no pixels will be rendered due to the cullface).
    // To solve this we must render the backfacing triangles of our sphere only using:
    glFrontFace(GL_CW);
	// Enable Depth test to compute the overlapping pixels
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
    // Activate blending to join the illumination of the lights inside the sphere with the scene that is already in the framebuffer
	glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
	
    //this deferred_ws shader uses the basic.vs instead of quad.vs
	shader = Shader::Get("deferred_ws");

	shader->enable();
	shader->setTexture("u_color_texture", gbuffers_fbo.color_textures[0], GTR::eChannels::ALBEDO);
	shader->setTexture("u_normal_texture", gbuffers_fbo.color_textures[1], GTR::eChannels::NORMAL);
	shader->setTexture("u_extra_texture", gbuffers_fbo.color_textures[2], GTR::eChannels::EMISSIVE);
	shader->setTexture("u_depth_texture", gbuffers_fbo.depth_texture, GTR::eChannels::DEPTH);
	
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_inverse_viewprojection", inv_vp);
	shader->setUniform("u_iRes", iRes);
	shader->setUniform("u_camera_position", camera->eye );
	
	Matrix44 m; Vector3 pos;
	for (int i = 0; i < this->light_entities.size(); i++)
	{
		light = this->light_entities[i];
		if (light->light_type == DIRECTIONAL)
			continue;
		
		//we must translate the model to the center of the light
		// and scale it according to the max_distance of the light
		float max_dist = light->max_dist;
		pos = light->model.getTranslation();
		m.setTranslation(pos.x, pos.y, pos.z);
		m.scale(max_dist, max_dist, max_dist);
		shader->setUniform("u_model", m); //pass the model to render the sphere

		light->uploadToShader(shader);
		sphere->render(GL_TRIANGLES);
		

		//only pixels behind a surface are rendered //only draw if the pixel is behind 
		// we solve this during the depth test stage, meaning before execte .fs
		
		//glBlendFunc(GL_ONE, GL_ZERO);
	}
    
    // Disabling CULL FACE
    glDisable(GL_CULL_FACE);
    // Setting back to render the front face
    glFrontFace(GL_CCW);
    // Disabling the depth test
    glDisable(GL_DEPTH_TEST);
    // Setting back to less depth test function
    glDepthFunc(GL_LESS);
    //now we can activate writing to depth buffer
    glDepthMask(GL_TRUE);
    // Disabling blending
    glDisable(GL_BLEND);
    
    // Render Irradiance
    renderIrradiance(scene, camera);
    
    // Volumetric rendering
    volumetricRendering(scene, camera);

	//stop rendering to the fbo
	illumination_fbo.unbind();
    
	
}


void Renderer::applyfinalHDR() { //function to apply HDR gamma !! postprocesing

	Mesh* quad = Mesh::getQuad();
	Shader* shader = Shader::Get("applyHDRgamma");
	shader->enable();
	shader->setTexture("u_texture", illumination_fbo.color_textures[0], 9); /////////change the number-----
	quad->render(GL_TRIANGLES);

}


//renders all the prefab
void Renderer::getRCsfromPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera, std::vector<RenderCall>& rc_vector)
{
	assert(prefab && "PREFAB IS NULL");
	//assign the model to the root node

	getRCsfromNode(model, &prefab->root, camera, rc_vector);
	
}


//renders a node of the prefab and its children
void Renderer::getRCsfromNode(const Matrix44& prefab_model, GTR::Node* node, Camera* camera, std::vector<RenderCall> &rc_vector)
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
		if (!camera || camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize) )
		{
			//instance each rc
			RenderCall rc;
			rc.model = node_model;
			rc.material = node->material;
			rc.mesh = node->mesh;
			if (camera) {

				rc.dist2camera = camera->eye.distance(world_bounding.center);
			}
			//this->rc_data_list.push_back(rc);
			rc_vector.push_back(rc);
			
			
			//node->mesh->renderBounding(node_model, true);
		}
	}


	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		getRCsfromNode(prefab_model, node->children[i], camera, rc_vector);
	
}


//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(eRenderMode mode, const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material )
		return;
    assert(glGetError() == GL_NO_ERROR);


	//define locals to simplify coding
	Shader* shader = NULL;
	GTR::Scene* scene = GTR::Scene::instance;

	//select if render both sides of the triangles
	if(material->two_sided)
		glDisable(GL_CULL_FACE); 
	else
		glEnable(GL_CULL_FACE);
    assert(glGetError() == GL_NO_ERROR); 

	//select shader with respect to the mode 
	if (mode == SHOW_TEXTURE)
		shader = Shader::Get("texture");

	else if (mode == SINGLE)
		shader = Shader::Get("light_singlepass");

	else if (mode ==  MULTI)
		shader = Shader::Get("light");

	else if (mode == SHOW_NORMAL) {
		shader = Shader::Get("sh2debug");
		shader->enable();
		shader->setUniform("u_texture_type", 0);
	}
	else if (mode == SHOW_OC) {
		shader = Shader::Get("sh2debug");
		shader->enable();
		shader->setUniform("u_texture_type", 1);
	}
	else if (mode == SHOW_UVS) {
		shader = Shader::Get("sh2debug");
		shader->enable();///---------------------------------------------
		shader->setUniform("u_texture_type", 2);
	}
	else if (mode == GBUFFERS) 
		shader = Shader::Get("gbuffers");

	if (!shader)//no shader? then nothing to render
		return;

	shader->enable();

	bool use_dither = false;
	//flag para deffered en materiales con transparencias.y
	if (mode == GBUFFERS && material->alpha_mode == GTR::eAlphaMode::BLEND) {
		shader->setUniform("u_use_dither", !use_dither);

	}
		
	/*if (mode == GBUFFERS && this->ao_buffer) {
		shader->setTexture("u_ao_texture", this->ao_buffer, GTR::eChannels::OCCLUSION);
	}*/
	
	//assert(glGetError() == GL_NO_ERROR);

	shader->setUniform("u_viewprojection", camera->viewprojection_matrix); //camera VP 
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model );
	//float t = getTime(); shader->setUniform("u_time", t);
	shader->setUniform("u_emissive_factor", material->emissive_factor);
	shader->setUniform("u_color", material->color);

	uploadTextures(material, shader);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	shader->setUniform("u_ambient_light", scene->ambient_light);//_---------------------------------

	//select the blending. Solo para las luces.
	if (material->alpha_mode == GTR::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	}
	else
		glDisable(GL_BLEND);

	if (mode == GTR::eRenderMode::SINGLE || mode == GTR::eRenderMode::MULTI) {
		
		renderlights(mode, shader, mesh, material);
		shader->disable();
		glDisable(GL_BLEND);
		
		return;
	}

	mesh->render(GL_TRIANGLES);
	shader->disable();
	glDisable(GL_BLEND); //set the render state as it was before to avoid problems with future renders

}

void Renderer::uploadTextures(Material* material, Shader* shader) {
	//create and load texture 
	Texture* texture = NULL;
	Texture* em_texture = NULL;
	Texture* mr_texture = NULL;
	Texture* oc_texture = NULL;
	Texture* n_texture = NULL;

	texture = material->color_texture.texture;
	em_texture = material->emissive_texture.texture;
	mr_texture = material->metallic_roughness_texture.texture;
	oc_texture = material->occlusion_texture.texture;
	n_texture = material->normal_texture.texture;


	if (texture == NULL)
		texture = Texture::getWhiteTexture(); //a 1x1 white texture
	if (em_texture == NULL)
		em_texture = Texture::getWhiteTexture();
	if (mr_texture == NULL)
		mr_texture = Texture::getBlackTexture();
	if (oc_texture == NULL)
		oc_texture = Texture::getWhiteTexture();
	if (n_texture == NULL)
		n_texture = Texture::getBlackTexture();

	//upload textures
	if (texture)
		shader->setTexture("u_color_texture", texture, 0);
	if (em_texture)
		shader->setTexture("u_emissive_texture", em_texture, 1);
	if (mr_texture)
		shader->setTexture("u_metallic_roughness_texture", mr_texture, 2);
	if (oc_texture)
		shader->setTexture("u_occlusion_texture", oc_texture, 3);
	if (n_texture)
		shader->setTexture("u_normal_texture", n_texture, 4);
}


void Renderer::renderlights(eRenderMode mode, Shader* shader, Mesh* mesh, GTR::Material* material) {
	
	if (!shader)
		return;
	
	if (mode == eRenderMode::MULTI) {

		for (int i = 0; i < this->light_entities.size(); ++i) {

			LightEntity* light = this->light_entities[i];

			// first pass we don't use blending
			if (i == 0 && material->alpha_mode != BLEND)
			{
				glDisable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			}
			else {
				glEnable(GL_BLEND);//enable blending and add the pixels to the previous ones
				glDepthFunc(GL_LEQUAL);//paints the pixels if it is LESS OR EQUAL of Zdepth
				glBlendFunc(GL_ONE, GL_ONE);

			}
			//pass the lights data to the shader
			light->uploadToShader(shader);
			mesh->render(GL_TRIANGLES); 			
			//only one pass ambient light and emissive light
			shader->setUniform("u_ambient_light", Vector3(0, 0, 0));
			shader->setUniform("u_emissive_factor", Vector3(0, 0, 0));

		} // loop of multipass

		glDisable(GL_BLEND);
		glDepthFunc(GL_LESS);

		return; //we put return, to go out when it finish!
	} // flag of multipass
	
	if (mode == eRenderMode::SINGLE) {
		std::vector <LightEntity*> lights = this->light_entities;
		const int num_lights = 8;
		
		int light_type[num_lights];
		Vector3 light_color[num_lights];
		Vector3 light_position[num_lights];
		Vector3 light_direction[num_lights];
		float light_intensity[num_lights];
		float light_maxdist[num_lights];
		float light_spot_exp[num_lights];
		float light_spot_cutoff[num_lights];
		float light_area_size[num_lights];
		//fill the elements of the lists
		for (int i = 0; i < lights.size(); i++)
		{
			if (i > num_lights - 1 )
				break; //finish when the have used all posibles lights of the list
			
			light_type[i] = lights[i]->light_type;
			light_color[i] = lights[i]->color;
			light_position[i] = lights[i]->model.getTranslation();
			light_direction[i] = lights[i]->model.frontVector();
			light_intensity[i] = lights[i]->intensity;
			light_maxdist[i] = lights[i]->max_dist;
			light_spot_cutoff[i] = cosf(lights[i]->cone_angle * DEG2RAD);
			light_spot_exp[i] = lights[i]->spot_exp;
			light_area_size[i] = lights[i]->area_size;

		}
				
		shader->setUniform1("u_num_lights", num_lights);
		shader->setUniform1Array("u_light_type", (int*)&light_type, num_lights);
		shader->setUniform3Array("u_light_color", (float*)&light_color, num_lights);
		shader->setUniform3Array("u_light_position", (float*)&light_position, num_lights);
		shader->setUniform3Array("u_light_vector", (float*)&light_direction, num_lights);
		shader->setUniform1Array("u_light_intensity", (float*)&light_intensity, num_lights);
		shader->setUniform1Array("u_light_maxdist", (float*)&light_maxdist, num_lights);

		shader->setUniform1Array("u_light_spotCosineCutoff", (float*)&light_spot_cutoff, num_lights);
		shader->setUniform1Array("u_light_spotExponent", (float*)&light_spot_exp, num_lights);
		shader->setUniform1Array("u_light_area_size", (float*)&light_area_size, num_lights);
		
		mesh->render(GL_TRIANGLES);

		return;
	}

}



void Renderer::createShadowmap( GTR::Scene* scene, Camera* camera) {
	
	int width = Application::instance->window_width;
	//int height = Application::instance->window_height;

	this->rendering_shadowmap = true;
	std::vector<RenderCall> rc_data_lights;
	LightEntity* light;

	for (int i = 0; i < this->light_entities.size(); i++)
	{
		if (!this->light_entities[i]->cast_shadows || this->light_entities[i]->light_type == POINT)
			continue;
		light = this->light_entities[i];
		
		collectRenderCalls(scene, light->light_camera, rc_data_lights);

		//enable it to render inside the texture
		light->shadow_fbo->bind();

		//you can disable writing to the color buffer to speed up the rendering as we do not need it
		glColorMask(false, false, false, false);

		//clear the depth buffer only (don't care of color)
		glClear(GL_DEPTH_BUFFER_BIT);

		//whatever we render here will be stored inside a texture, we don't need to do anything fanzy

		for (int i = 0; i < rc_data_lights.size(); i++) {
			renderMesh(rc_data_lights[i].model, rc_data_lights[i].mesh, light->light_camera, rc_data_lights[i].material->alpha_mode);
		}
		
		//disable it to render back to the screen
		light->shadow_fbo->unbind();

		//allow to render back to the color buffer
		glColorMask(true, true, true, true);
	

	}
	
	/*
	//Clear rendercall for framebuffer ¿¿??
	
	*/


	//glDisable(GL_DEPTH_TEST);
	//glViewport(0, 0, width * 0.2, width * 0.2);
	//light->shadow_fbo->depth_texture->toViewport();

	update_shadowmaps = false;

}

void Renderer::renderMesh(const Matrix44 model, Mesh* mesh, Camera* camera, eAlphaMode material_alpha_mode){
    
    glDisable(GL_BLEND);
    //in case there is nothing to do
    if (!mesh || !mesh->getNumVertices())
        return;
    assert(glGetError() == GL_NO_ERROR);
    
    Shader* shader = NULL;
    
    // If blending, then we won't draw anything
    if (material_alpha_mode == GTR::eAlphaMode::BLEND)
		return;

    //chose a shader
    shader = Shader::Get("flat");

    assert(glGetError() == GL_NO_ERROR);

    //no shader? then nothing to render
    if (!shader)
        return;
    shader->enable();
    
    //upload uniforms
    shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
    shader->setUniform("u_model", model );
    
    mesh->render(GL_TRIANGLES);
    
}


void Renderer::showShadowmap(Camera* camera) {
	

	int width = Application::instance->window_width;
	int height = Application::instance->window_height;

	LightEntity* light;
	int cont_ini_pos = 0;
	for (int i = 0; i < this->light_entities.size(); i++)
	{
		if (!this->light_entities[i]->cast_shadows || this->light_entities[i]->light_type == POINT ) //|| this->light_entities[i]->light_type == SPOT
			continue;
		light = this->light_entities[i];

		
		//remember to disable ztest if rendering quads!
		glDisable(GL_DEPTH_TEST);

		glViewport(cont_ini_pos, 0, width * 0.2, width * 0.2);
		cont_ini_pos += width * 0.2;
		//to use a special shader,  to visualize a Depth Texture
		Shader* zshader = Shader::Get("depth");
		zshader->enable();
		//zshader->setTexture("u_texture", light->shadow_fbo->depth_texture, 10); //-----------------
		zshader->setUniform("u_camera_nearfar", Vector2(camera->near_plane, camera->far_plane));
		light->shadow_fbo->depth_texture->toViewport(zshader);
		
		
		
	}
	glViewport(0, 0, width, height);

}


//---Screen Space Ambient Occlusion

//genera puntos equidistances sobre la esfera
std::vector<Vector3> generateSpherePoints(int num, float radius, bool hemi)
{
	std::vector<Vector3> points;
	points.resize(num);
	for (int i = 0; i < num; i += 3)
	{
		Vector3& p = points[i];
		float u = randomFramework(); // Le he tenido que cambiar el nombre por que en MacOS es ambigua
		float v = randomFramework();
		float theta = u * 2.0 * PI;
		float phi = acos(2.0 * v - 1.0);
		//float r = cbrt(random()) * radius;
		float r = radius;
		float sinTheta = sin(theta);
		float cosTheta = cos(theta);
		float sinPhi = sin(phi);
		float cosPhi = cos(phi);
		p.x = r * sinPhi * cosTheta;
		p.y = r * sinPhi * sinTheta;
		p.z = r * cosPhi;
		if (hemi && p.z < 0)
			p.z *= -1.0;
	}
	return points;
}

SSAOFX::SSAOFX() {
	this->intensity = 1.0;
	
	random_points = generateSpherePoints(64*4, 1.0, true);

}

void SSAOFX::applyEffect(Texture* Zbuffer, Texture* normal_buffer, Camera* camera, Texture* outputOcc) {

	
	FBO* ssao_fbo = Texture::getGlobalFBO(outputOcc);
	//start rendering inside the ssao texture
	ssao_fbo->bind();

	//disable using mipmaps
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	//enable bilinear filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	Mesh* quad = Mesh::getQuad();
	glDisable(GL_DEPTH_TEST); //pintar quad desactivas los flags...
	glDisable(GL_BLEND);

	//get the shader for SSAO (remember to create it using the atlas)
	Shader* shader = Shader::Get("ssao");
	shader->enable();
	//send random points so we can fetch around
	shader->setUniform3Array("u_points", random_points[0].v, random_points.size()); // numero de vectores que hay 

	shader->setTexture("u_normal_texture", normal_buffer, GTR::eChannels::NORMAL);
	shader->setTexture("u_depth_texture", Zbuffer, GTR::eChannels::DEPTH);
	
	//send info to reconstruct the world position nd iRes (pixel size) to center the samples
	//we will need the viewprojection to obtain the uv in the depthtexture of any random position of our world
	
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix );
	Matrix44 invp = camera->viewprojection_matrix;
	invp.inverse();
	shader->setUniform("u_inverse_viewprojection", invp);

	shader->setUniform("u_iRes", Vector2(1.0 / (float)Zbuffer->width, 1.0 / (float)Zbuffer->height));
	shader->setUniform("u_camera_nearfar", Vector2(camera->near_plane, camera->far_plane));
	//render fullscreen quad
	quad->render(GL_TRIANGLES);

	ssao_fbo->unbind();


}


//--------IRRADIANCE_-PROBE-------------------------------------------------------------------------------------------------------------------------
// 
//rendering to a texture
void Renderer::extractProbe(GTR::Scene* scene, sProbe& p) {//es una ref pq internamente se va a modificar
	//vamos a necesitar un fbo, pq vamos a renderizarlo en un espacio separado, no pantalla
	FloatImage images[6]; //here we will store the six views
	Camera camera;

	//set the fov to 90 and the aspect to 1
	camera.setPerspective(90, 1, 0.1, 1000);

	
	collectRenderCalls(scene, NULL, this->rc_data_list);//extraer todos los objetos sin camara

	for (int i = 0; i < 6; ++i) //for every cubemap face
	{
		//compute camera orientation using defined vectors
		Vector3 eye = p.pos;
		Vector3 front = cubemapFaceNormals[i][2]; //vector hacia delante y hacia arriba...
		Vector3 center = p.pos + front;
		Vector3 up = cubemapFaceNormals[i][1];
		camera.lookAt(eye, center, up);
		camera.enable();

		//render the scene from this point of view
		irr_fbo.bind();
		renderForward(scene, this->rc_data_list, &camera, true); //como solo tenemos un canal, los buffers de deferred tienen resolucion de la pantalla...

		irr_fbo.unbind();

		//read the pixels back and store in a FloatImage !!
		images[i].fromTexture(irr_fbo.color_textures[0]); //leer de vuelta desde GPU a CPU
	}

	//compute the coefficients given the six images
	bool gammaCorrection = false;
	p.sh = computeSH(images, gammaCorrection);
}


void Renderer::createProbeGrid(Vector3 dim, Vector3 start_pos, Vector3 end_pos) {

	//when computing the probes position…
	//define the corners of the axis aligned grid
	//this can be done using the boundings of our scene
	this->probe_dim = dim;
	this->probe_start_pos = start_pos;
	this->probe_end_pos = end_pos;

	//compute the vector from one corner to the other
	this->probe_delta = (end_pos - start_pos);


	//and scale it down according to the subdivisions
	//we substract one to be sure the last probe is at end pos
	this->probe_delta.x /= (dim.x - 1);
	this->probe_delta.y /= (dim.y - 1);
	this->probe_delta.z /= (dim.z - 1);
	//now delta give us the distance between probes in every axis


}

void Renderer::placingProbes() {

	//lets compute the centers
	//pay attention at the order at which we add them:
	// The order we add them is x, y and z.
	for (int z = 0; z < this->probe_dim.z; ++z)
	{
		for (int y = 0; y < this->probe_dim.y; ++y)
		{
			for (int x = 0; x < this->probe_dim.x; ++x)
			{
				sProbe p;
				// Index in the tridimensional matrix
				p.local.set(x, y, z);
				//index in the linear array
				p.index = x + y * probe_dim.x + z * probe_dim.x * probe_dim.y;
				//and its position
				p.pos = probe_start_pos + probe_delta * Vector3(x, y, z);
				probes.push_back(p);
			}
		}
	}

}

void Renderer::renderProbe(Vector3 pos, float size, float* coeffs)
{
	Camera* camera = Camera::current;

	Shader* shader = Shader::Get("probe");
	Mesh* mesh = Mesh::Get("data/meshes/sphere.obj", false, false);
	
	glEnable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	Matrix44 model;
	model.setTranslation(pos.x, pos.y, pos.z);
	model.scale(size, size, size);

	
	shader->enable();
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);

	shader->setUniform3Array("u_coeffs", coeffs, 9); //vienen despues

	mesh->render(GL_TRIANGLES);
}

void Renderer::renderProbesGrid()
{

	for (int i = 0; i < probes.size(); i++)
	{
		Vector3 pos = probes[i].pos;
		float size = 10.0;
		SphericalHarmonics sh = probes[i].sh;

		renderProbe(pos, size, sh.coeffs[0].v);
	}
}

void Renderer::computeProbesCoeff(GTR::Scene* scene) {

	int num = (int)probes.size();
	//now compute the coeffs for every probe
	for (int iP = 0; iP < num; ++iP)
	{
		//int probe_index = iP;
		//...
		extractProbe(scene, probes[iP]);
	}
}

void Renderer::createProbesTexture() {
	//create the texture to store the probes (do this ONCE!!!)
	this->probes_texture = new Texture(
		9, //9 coefficients per probe
		(int)probes.size(), //as many rows as probes
		GL_RGB, //3 channels per coefficient
		GL_FLOAT); //they require a high range
}

void Renderer::fillAndUploadProbesTextureToGPU() {
	int sh_size = (int)probes.size();

	// Check that the vector probes has values.
	// If not, we do not need to upload anything to the GPU
	if (sh_size == 0) {
		return;
	}

	// we must create the color information for the texture. because every SH are 27 floats in the RGB,RGB,... order,
	// we can create an array of SphericalHarmonics and use it as pixels of the texture
	SphericalHarmonics* sh_data = new SphericalHarmonics[sh_size];

	//here we fill the data of the array with our probes in x,y,z order...
	for (int i = 0; i < sh_size; i++) {
		// Since the probes were saved in the x, y, z order, we can iterate them this way to the sh in x, y, z order.
		sh_data[i] = probes[i].sh;

	}

	//now upload the data to the GPU
	probes_texture->upload(GL_RGB, GL_FLOAT, false, (uint8*)sh_data);

	//disable any texture filtering when reading
	probes_texture->bind(); // No entiendo para qué es el 0 que se le pasa como parametro en las slides.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	probes_texture->unbind();
	//always free memory after allocating it!!!
	delete[] sh_data;
}

void Renderer::updateIrradianceCache(GTR::Scene* scene) {//para hacer actualizaciones del probe
	if (!updateIrradiance) {
		return;
	}
	computeProbesCoeff(scene);
	fillAndUploadProbesTextureToGPU();
	this->updateIrradiance = false;
}

void Renderer::uploadIrradianceUniforms(Shader* shader, Camera* camera) {
	// Hardcodeo las normal distance por ahora, pero lo podríamos poner como parte del renderer
	float irr_normal_distance = 10.0;

	if (shader == NULL) {
		return;
	}
    
    // Compute the inverse of viewprojection matrix
    Matrix44 inv_vp = camera->viewprojection_matrix;
    inv_vp.inverse();

	shader->enable();

	shader->setUniform("u_irr_start", probe_start_pos);
	shader->setUniform("u_irr_end", probe_end_pos);
	shader->setUniform("u_irr_normal_distance", irr_normal_distance);
	shader->setUniform("u_irr_delta", probe_delta);
	shader->setUniform("u_irr_dims", probe_dim);
	shader->setUniform("u_num_probes", (float)probes.size());
	shader->setTexture("u_probes_texture", probes_texture, GTR::eChannels::PROBE);
    
    int width = Application::instance->window_width;
    int height = Application::instance->window_height;
    
    Vector2 iRes = Vector2(1.0 / (float)width, 1.0 / (float)height);
    
    shader->setUniform("u_iRes", iRes);
    shader->setUniform("u_inverse_viewprojection", inv_vp);
    shader->setTexture("u_depth_texture", gbuffers_fbo.depth_texture, GTR::eChannels::DEPTH); // We use the depth texture to reconstruct the 3D world position in the shader
    shader->setTexture("u_normal_texture", gbuffers_fbo.color_textures[1], GTR::eChannels::NORMAL);
    shader->setTexture("u_color_texture", gbuffers_fbo.color_textures[0], GTR::eChannels::ALBEDO);

}

void Renderer::renderIrradiance(Scene* scene, Camera* camera){
    updateIrradianceCache(scene);
    
    if (!show_irradiance){
        return;
    }
    Mesh* quad = Mesh::getQuad();
    Shader* shader = Shader::Get("irradiance_sh");
    uploadIrradianceUniforms(shader, camera);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    quad->render(GL_TRIANGLES);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    
    // to render the probes on the screen to visualize it
    if(!show_probes){
        return;
    }
    renderProbesGrid();
}

void Renderer::volumetricRendering(Scene* scene, Camera* camera){
    if(!show_volumetric_rendering)
        return;
    
    float air_density = 0.001;
    
    int width = Application::instance->window_width;
    int height = Application::instance->window_height;
    Vector2 iRes = Vector2(1.0 / (float)width, 1.0 / (float)height);
    
    for (int i = 0; i < light_entities.size(); i++){
        LightEntity* light = light_entities[i];
        if(light->light_type != DIRECTIONAL)
            continue;
        
        
        Mesh* quad = Mesh::getQuad();
        Shader* shader = Shader::Get("volumetric_rendering");
        shader->enable();
        light->uploadToShader(shader);
        
        Matrix44 inv_vp = camera->viewprojection_matrix;
        inv_vp.inverse();
        
        shader->setUniform("u_inverse_viewprojection", inv_vp);
        shader->setUniform("u_iRes", iRes);
        shader->setUniform("u_camera_position", camera->eye);
        shader->setTexture("u_depth_texture", gbuffers_fbo.depth_texture, eChannels::DEPTH);
        
        shader->setUniform("u_air_density", air_density);
        
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        quad->render(GL_TRIANGLES);
        glDisable(GL_BLEND);
        
        
    }
}
//--------------------------HDRE------------------------------------------------------------

void GTR::Renderer::renderSkybox(Texture* skybox, Camera* camera)
{

	//Mesh* mesh = Mesh::getQuad();
	Mesh* mesh = Mesh::Get("data/meshes/sphere.obj", false, false);
	
	Shader* shader = Shader::Get("skybox");
	shader->enable();

	// pasamos model y wp
	Matrix44 m;
	m.scale(10, 10, 10);
	m.translate(camera->eye.x, camera->eye.y, camera->eye.z);

	shader->setUniform("u_model", m);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setTexture("u_texture", skybox , 0); /////////change the number-----
	
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	//no nos ha midificado el zbuffer
	mesh->render(GL_TRIANGLES);
	
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);

	
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

void Renderer::renderDecals(Camera* camera) 
{
	//For each decal, we need to read the zbuffer, draw a cube and reproject the decal to the surface 

	Mesh* cube = Mesh::getCube(); 
	

	Shader* shader = Shader::Get("decal");
	shader->enable();

	shader->setTexture("u_color_texture", gbuffers_fbo.color_textures[0], GTR::eChannels::ALBEDO);
	shader->setTexture("u_normal_texture", gbuffers_fbo.color_textures[1], GTR::eChannels::NORMAL);
	shader->setTexture("u_extra_texture", gbuffers_fbo.color_textures[2], GTR::eChannels::EMISSIVE);
	shader->setTexture("u_depth_texture", gbuffers_fbo.depth_texture, GTR::eChannels::DEPTH);

	shader->setUniform("u_inverse_viewprojection", camera->inverse_viewprojection_matrix);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	Vector2 iRes = Vector2(1.0 / (float)gbuffers_fbo.depth_texture->width, 1.0 / (float)gbuffers_fbo.depth_texture->height);
	shader->setUniform("u_iRes", iRes);

	for (int i = 0; i < this->decal_entities.size(); i++)
	{
		DecalEntity* decal = this->decal_entities[i];

		shader->setUniform("u_model", decal->model);
		Matrix44 invModel = decal->model;
		invModel.inverse();
		shader->setUniform("u_iModel", invModel);
		shader->setUniform("u_decal_texture_type", decal->texture_type);

		shader->setTexture("u_decal_texture", decal->decal_texture, 4);
		
		cube->render(GL_TRIANGLES);
	}

	

}
