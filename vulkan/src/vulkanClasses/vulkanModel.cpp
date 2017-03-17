

#include "vulkanModel.h"

namespace vkx {

	//http://stackoverflow.com/questions/12927169/how-can-i-initialize-c-object-member-variables-in-the-constructor
	//http://stackoverflow.com/questions/14169584/passing-and-storing-a-const-reference-via-a-constructor


	Model::Model(vkx::Context *context, vkx::AssetManager *assetManager) {
		this->meshLoader = new vkx::MeshLoader(context, assetManager);
	}


	void Model::load(const std::string &filename) {
		this->meshLoader->load(filename);
	}

	void Model::load(const std::string &filename, int flags) {
		this->meshLoader->load(filename, flags);
	}


	void Model::createMeshes(const std::vector<VertexLayout> &layout, float scale, uint32_t binding) {

		this->meshLoader->createMeshBuffers(layout, scale);

		std::vector<MeshBuffer> meshBuffers = this->meshLoader->meshBuffers;

		for (int i = 0; i < meshBuffers.size(); ++i) {
			vkx::Mesh m(meshBuffers[i]);
			this->meshes.push_back(m);
		}
		
		// todo: destroy this->meshLoader->meshBuffers here:
		//for (int i = 0; i < this->meshLoader->meshBuffers.size(); ++i) {
		//	// destroy here
		//}

		//this->materials = this->meshLoader->materials;
		//this->attributeDescriptions = this->meshLoader->attributeDescriptions;

		this->vertexBufferBinding = binding;// important
		//this->setupVertexInputState(layout);// doesn't seem to be necessary/used
		//this->bindingDescription = this->meshLoader->bindingDescriptions[0];// ?
		//this->pipeline = this->meshLoader->pipeline;// not needed?
	}

	void Model::destroy() {
		for (auto &mesh : this->meshes) {
			mesh.meshBuffer.destroy();
		}
		// todo:
		// more to delete:
		delete this->meshLoader;
	}



}