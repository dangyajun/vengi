/**
 * @file
 */

#include "ShaderTool.h"
#include "core/App.h"
#include "core/Process.h"
#include "core/Tokenizer.h"
#include "core/GameConfig.h"
#include "video/Shader.h"

const ShaderTool::Types ShaderTool::cTypes[] = {
	{ ShaderTool::Variable::FLOAT,           1, "float",            Value },
	{ ShaderTool::Variable::UNSIGNED_INT,    1, "unsigned int",     Value },
	{ ShaderTool::Variable::INT,             1, "int",              Value },
	{ ShaderTool::Variable::IVEC2,           2, "const glm::ivec2", Reference },
	{ ShaderTool::Variable::IVEC3,           3, "const glm::ivec3", Reference },
	{ ShaderTool::Variable::IVEC4,           4, "const glm::ivec4", Reference },
	{ ShaderTool::Variable::VEC2,            2, "const glm::vec2",  Reference },
	{ ShaderTool::Variable::VEC3,            3, "const glm::vec3",  Reference },
	{ ShaderTool::Variable::VEC4,            4, "const glm::vec4",  Reference },
	{ ShaderTool::Variable::MAT3,            1, "const glm::mat3",  Reference },
	{ ShaderTool::Variable::MAT4,            1, "const glm::mat4",  Reference },
	{ ShaderTool::Variable::SAMPLER2D,       1, "int",              Value },
	{ ShaderTool::Variable::SAMPLER2DSHADOW, 1, "int",              Value }
};

ShaderTool::ShaderTool(io::FilesystemPtr filesystem, core::EventBusPtr eventBus) :
		core::App(filesystem, eventBus, 0) {
	init("engine", "shadertool");
	static_assert(Variable::MAX == SDL_arraysize(cTypes), "mismatch in glsl types");
}

ShaderTool::~ShaderTool() {
}

std::string ShaderTool::uniformSetterPostfix(const ShaderTool::Variable::Type type, int amount) const {
	switch (type) {
	case Variable::MAX:
		return "";
	case Variable::FLOAT:
		if (amount > 1) {
			return "fv";
		}
		return "f";
	case Variable::UNSIGNED_INT:
		if (amount > 1) {
			return "uiv";
		}
		return "ui";
	case Variable::INT:
		if (amount > 1) {
			return "iv";
		}
		return "i";
	case Variable::VEC2:
		if (amount > 1) {
			return "Vec2v";
		}
		return "Vec2";
	case Variable::VEC3:
		if (amount > 1) {
			return "Vec3v";
		}
		return "Vec3";
	case Variable::VEC4:
		if (amount > 1) {
			return "Vec4v";
		}
		return "Vec4";
	case Variable::IVEC2:
		if (amount > 1) {
			return "Vec2v";
		}
		return "Vec2";
	case Variable::IVEC3:
		if (amount > 1) {
			return "Vec3v";
		}
		return "Vec3";
	case Variable::IVEC4:
		if (amount > 1) {
			return "Vec4v";
		}
		return "Vec4";
	case Variable::MAT3:
	case Variable::MAT4:
		if (amount > 1) {
			return "Matrixv";
		}
		return "Matrix";
	case Variable::SAMPLER2D:
		if (amount > 1) {
			return "iv";
		}
		return "i";
	case Variable::SAMPLER2DSHADOW:
		if (amount > 1) {
			return "iv";
		}
		return "i";
	}
	return "";
}

int ShaderTool::getComponents(const ShaderTool::Variable::Type type) const {
	return cTypes[(int)type].typeSize;
	switch (type) {
	case Variable::FLOAT:
	case Variable::UNSIGNED_INT:
	case Variable::INT:
		return 1;
	case Variable::IVEC2:
	case Variable::VEC2:
		return 2;
	case Variable::IVEC3:
	case Variable::VEC3:
		return 3;
	case Variable::IVEC4:
	case Variable::VEC4:
		return 4;
	case Variable::MAT3:
		return 9;
	case Variable::MAT4:
		return 16;
	case Variable::SAMPLER2D:
	case Variable::SAMPLER2DSHADOW:
	case Variable::MAX:
		return -1;
	}
	return -1;
}

ShaderTool::Variable::Type ShaderTool::getType(const std::string& type) const {
	if (type == "float") {
		return Variable::FLOAT;
	} else if (type == "int") {
		return Variable::INT;
	} else if (type == "uint") {
		return Variable::INT;
	} else if (type == "vec2") {
		return Variable::VEC2;
	} else if (type == "vec3") {
		return Variable::VEC3;
	} else if (type == "vec4") {
		return Variable::VEC4;
	} else if (type == "uvec2") {
		return Variable::VEC2;
	} else if (type == "uvec3") {
		return Variable::VEC3;
	} else if (type == "uvec4") {
		return Variable::VEC4;
	} else if (type == "ivec2") {
		return Variable::IVEC2;
	} else if (type == "ivec3") {
		return Variable::IVEC3;
	} else if (type == "ivec4") {
		return Variable::IVEC4;
	} else if (type == "mat3") {
		return Variable::MAT3;
	} else if (type == "mat4") {
		return Variable::MAT4;
	} else if (type == "sampler2D") {
		return Variable::SAMPLER2D;
	} else if (type == "sampler2DShadow") {
		return Variable::SAMPLER2DSHADOW;
	}
	core_assert_msg(false, "unknown type given: %s", type.c_str());
	return Variable::FLOAT;
}

void ShaderTool::generateSrc() const {
	for (const auto& v : _shaderStruct.uniforms) {
		Log::debug("Found uniform of type %i with name %s", int(v.type), v.name.c_str());
	}
	for (const auto& v : _shaderStruct.attributes) {
		Log::debug("Found attribute of type %i with name %s", int(v.type), v.name.c_str());
	}
	for (const auto& v : _shaderStruct.varyings) {
		Log::debug("Found varying of type %i with name %s", int(v.type), v.name.c_str());
	}
	for (const auto& v : _shaderStruct.outs) {
		Log::debug("Found out var of type %i with name %s", int(v.type), v.name.c_str());
	}

	const std::string& templateShader = core::App::getInstance()->filesystem()->load(_shaderTemplateFile);
	std::string src(templateShader);
	std::string name = _shaderStruct.name + "Shader";

	std::vector<std::string> shaderNameParts;
	core::string::splitString(name, shaderNameParts, "_");
	std::string filename = "";
	for (std::string n : shaderNameParts) {
		if (n.length() > 1 || shaderNameParts.size() < 2) {
			n[0] = SDL_toupper(n[0]);
			filename += n;
		}
	}
	if (filename.empty()) {
		filename = name;
	}
	const std::string classname = filename;
	filename += ".h";
	src = core::string::replaceAll(src, "$name$", classname);
	src = core::string::replaceAll(src, "$namespace$", _namespaceSrc);
	src = core::string::replaceAll(src, "$filename$", _shaderDirectory + _shaderStruct.filename);
	std::stringstream uniforms;
	std::stringstream uniformArrayInfo;
	const int uniformSize = int(_shaderStruct.uniforms.size());
	if (uniformSize > 0) {
		uniforms << "checkUniforms({";
		for (int i = 0; i < uniformSize; ++i) {
			std::string uniformName = _shaderStruct.uniforms[i].name;
			uniforms << "\"";
			uniforms << uniformName;
			if (_shaderStruct.uniforms[i].arraySize == -1 || _shaderStruct.uniforms[i].arraySize > 1) {
				uniforms << "[0]";
			}
			uniforms << "\"";
			if (i < uniformSize - 1) {
				uniforms << ", ";
			}
		}
		uniforms << "});";

		for (int i = 0; i < uniformSize; ++i) {
			uniformArrayInfo << "\t\tsetUniformArraySize(\"";
			uniformArrayInfo << _shaderStruct.uniforms[i].name;
			uniformArrayInfo << "\", ";
			uniformArrayInfo << _shaderStruct.uniforms[i].arraySize;
			uniformArrayInfo << ");\n";
		}
	} else {
		uniforms << "// no uniforms";
	}
	src = core::string::replaceAll(src, "$uniformarrayinfo$", uniformArrayInfo.str());
	src = core::string::replaceAll(src, "$uniforms$", uniforms.str());

	std::stringstream attributes;
	const int attributeSize = int(_shaderStruct.attributes.size());
	if (attributeSize > 0) {
		attributes << "checkAttributes({";
		for (int i = 0; i < attributeSize; ++i) {
			const Variable& v = _shaderStruct.attributes[i];
			attributes << "\"" << v.name << "\"";
			if (i < attributeSize - 1) {
				attributes << ", ";
			}
		}
		attributes << "});\n";

		for (int i = 0; i < attributeSize; ++i) {
			const Variable& v = _shaderStruct.attributes[i];
			attributes << "\t\tconst int " << v.name << "Location = getAttributeLocation(\"" << v.name << "\");\n";
			attributes << "\t\tif (" << v.name << "Location != -1) {\n";
			attributes << "\t\t\tsetAttributeComponents(" << v.name << "Location, " << getComponents(v.type) << ");\n";
			attributes << "\t\t}\n";
		}
	} else {
		attributes << "// no attributes";
	}

	std::stringstream setters;
	if (uniformSize > 0 || attributeSize > 0) {
		setters << "\n";
	}
	for (int i = 0; i < uniformSize; ++i) {
		const Variable& v = _shaderStruct.uniforms[i];
		std::string uniformName = "";
		std::vector<std::string> nameParts;
		core::string::splitString(v.name, nameParts, "_");
		for (std::string n : nameParts) {
			if (n.length() > 1 || nameParts.size() < 2) {
				n[0] = SDL_toupper(n[0]);
				uniformName += n;
			}
		}
		if (uniformName.empty()) {
			uniformName = v.name;
		}
		setters << "\tinline bool set" << uniformName << "(";
		const Types& cType = cTypes[v.type];
		setters << cType.ctype;
		if (v.arraySize == -1 || cType.passBy == PassBy::Pointer) {
			setters << "*";
		} else if (cType.passBy == PassBy::Reference) {
			if (v.arraySize <= 0) {
				setters << "&";
			}
		} else if (cType.passBy == PassBy::Value) {
		}

		if (v.arraySize > 0) {
			setters << " (&" << v.name << ")";
		} else {
			setters << " " << v.name;
		}
		if (v.arraySize > 0) {
			setters << "[" << v.arraySize << "]";
		} else if (v.arraySize == -1) {
			setters << ", int amount";
		}
		setters << ") const {\n";

		setters << "\t\tif (!hasUniform(\"" << v.name;
		if (v.arraySize == -1 || v.arraySize > 1) {
			setters << "[0]";
		}
		setters << "\")) {\n";
		setters << "\t\t\treturn false;\n";
		setters << "\t\t}\n";
		setters << "\t\tsetUniform" << uniformSetterPostfix(v.type, v.arraySize == -1 ? 2 : v.arraySize);
		setters << "(\"" << v.name;
		if (v.arraySize == -1 || v.arraySize > 1) {
			setters << "[0]";
		}
		setters << "\", " << v.name;
		if (v.arraySize > 0) {
			setters << ", " << v.arraySize;
		} else if (v.arraySize == -1) {
			setters << ", amount";
		}
		setters << ");\n";
		setters << "\t\treturn true;\n";
		setters << "\t}\n";
		if (i < uniformSize- - 2) {
			setters << "\n";
		}

#if 0
		if (v.arraySize == -1 || v.arraySize > 1) {
			setters << "\tinline bool set" << uniformName << "(";
			const Types& cType = cTypes[v.type];
			setters << "const std::vector<" << cType.ctype << ">& " << v.name << ") const {\n";
			setters << "\t\tif (!hasUniform(\"" << v.name << "[0]\")) {\n";
			setters << "\t\t\treturn false;\n";
			setters << "\t\t}\n";
			setters << "\t\tsetUniform" << uniformSetterPostfix(v.type, v.arraySize == -1 ? 2 : v.arraySize);
			setters << "(\"" << v.name << "[0]\", &" << v.name << "[0], " << v.name << ".size());\n";
			setters << "\t\treturn true;\n";
			setters << "\t}\n";
			if (i < uniformSize- - 2) {
				setters << "\n";
			}
		}
#endif
	}
	for (int i = 0; i < attributeSize; ++i) {
		const Variable& v = _shaderStruct.attributes[i];
		std::string attributeName = "";
		std::vector<std::string> nameParts;
		core::string::splitString(v.name, nameParts, "_");
		for (std::string n : nameParts) {
			if (n.length() > 1 || nameParts.size() < 2) {
				n[0] = SDL_toupper(n[0]);
				attributeName += n;
			}
		}
		if (attributeName.empty()) {
			attributeName = v.name;
		}
		const bool isInt = v.type == Variable::UNSIGNED_INT || v.type == Variable::INT || v.type == Variable::IVEC2 || v.type == Variable::IVEC3 || v.type == Variable::IVEC4;
		setters << "\tinline bool init" << attributeName << "(GLsizei stride, const void* pointer, GLenum type = ";
		if (isInt) {
			setters << "GL_INT";
		} else {
			setters << "GL_FLOAT";
		}
		setters << ", GLint size = ";
		setters << cTypes[v.type].typeSize << ", ";
		setters << "bool isInt = ";
		setters << (isInt ? "true" : "false");
		setters << ", bool normalize = false) const {\n";
		setters << "\t\tif (!hasAttribute(\"" << v.name << "\")) {\n";
		setters << "\t\t\treturn false;\n";
		setters << "\t\t}\n";
		setters << "\t\tconst int loc = enableVertexAttributeArray(\"" << v.name << "\");\n";
		setters << "\t\tif (isInt) {\n";
		setters << "\t\t\tsetVertexAttributeInt(loc, size, type, stride, pointer);\n";
		setters << "\t\t} else {\n";
		setters << "\t\t\tsetVertexAttribute(loc, size, type, normalize, stride, pointer);\n";
		setters << "\t\t}\n";
		setters << "\t\treturn true;\n";
		setters << "\t}\n\n";
		setters << "\tinline int getLocation" << attributeName << "() const {\n";
		setters << "\t\treturn getAttributeLocation(\"" << v.name << "\");\n";
		setters << "\t}\n";
		if (i < attributeSize- - 2) {
			setters << "\n";
		}
	}

	src = core::string::replaceAll(src, "$attributes$", attributes.str());
	src = core::string::replaceAll(src, "$setters$", setters.str());
	const std::string targetFile = _sourceDirectory + filename;
	Log::debug("Generate shader bindings for %s at %s", _shaderStruct.name.c_str(), targetFile.c_str());
	core::App::getInstance()->filesystem()->syswrite(targetFile, src);
}

bool ShaderTool::parse(const std::string& buffer, bool vertex) {
	core::Tokenizer tok(buffer);
	while (tok.hasNext()) {
		const std::string token = tok.next();
		Log::trace("token: %s", token.c_str());
		std::vector<Variable>* v = nullptr;
		if (token == "$in") {
			if (vertex) {
				v = &_shaderStruct.attributes;
			} else {
				// TODO: use this to validate that each $out of the vertex shader has a $in in the fragment shader
				//v = &_shaderStruct.varyings;
			}
		} else if (token == "$out") {
			if (vertex) {
				v = &_shaderStruct.varyings;
			} else {
				v = &_shaderStruct.outs;
			}
		} else if (token == "uniform") {
			v = &_shaderStruct.uniforms;
		}

		if (v == nullptr) {
			continue;
		}

		if (!tok.hasNext()) {
			Log::error("Failed to parse the shader, could not get type");
			return false;
		}
		std::string type = tok.next();
		if (!tok.hasNext()) {
			Log::error("Failed to parse the shader, could not get variable name for type %s", type.c_str());
			return false;
		}
		while (type == "highp" || type == "mediump" || type == "lowp" || type == "precision") {
			if (!tok.hasNext()) {
				Log::error("Failed to parse the shader, could not get type");
				return false;
			}
			type = tok.next();
		}
		std::string name = tok.next();
		const Variable::Type typeEnum = getType(type);
		bool isArray = false;
		std::string number;
		for (char c : name) {
			if (c == ']') {
				break;
			}
			if (isArray) {
				number.push_back(c);
			}
			if (c == '[') {
				isArray = true;
			}
		}
		int arraySize = core::string::toInt(number);
		if (isArray && arraySize == 0) {
			arraySize = -1;
			Log::warn("Could not determine array size for %s (%s)", name.c_str(), number.c_str());
		}
		if (isArray) {
			std::vector<std::string> tokens;
			core::string::splitString(name, tokens, "[");
			if (tokens.size() != 2) {
				Log::error("Could not extract variable name from %s", name.c_str());
			} else {
				name = tokens[0];
			}
		}
		auto findIter = std::find_if(v->begin(), v->end(), [&] (const Variable& var) { return var.name == name; });
		if (findIter == v->end()) {
			v->push_back(Variable{typeEnum, name, arraySize});
		} else {
			Log::warn("Found duplicate uniform %s (%s versus %s)",
					name.c_str(), cTypes[(int)findIter->type].ctype, cTypes[(int)typeEnum].ctype);
		}
	}
	return true;
}

core::AppState ShaderTool::onRunning() {
	if (_argc < 4) {
		_exitCode = 1;
		Log::error("Usage: %s <path/to/glslangvalidator> <shaderfile> <shadertemplate> <namespace> <shader-dir> <src-generator-dir>", _argv[0]);
		return core::AppState::Cleanup;
	}

	const std::string glslangValidatorBin = _argv[1];
	const std::string shaderfile          = _argv[2];
	_shaderTemplateFile                   = _argv[3];
	_namespaceSrc    = _argc >= 5 ?         _argv[4] : "frontend";
	_shaderDirectory = _argc >= 6 ?         _argv[5] : "shaders/";
	_sourceDirectory = _argc >= 7 ?         _argv[6] : _filesystem->basePath() + "src/modules/" + _namespaceSrc + "/";

	Log::debug("Using glslangvalidator binary: %s", glslangValidatorBin.c_str());
	Log::debug("Using %s as output directory", _sourceDirectory.c_str());
	Log::debug("Using %s as namespace", _namespaceSrc.c_str());
	Log::debug("Using %s as shader directory", _shaderDirectory.c_str());

	Log::debug("Preparing shader file %s", shaderfile.c_str());
	const std::string fragmentFilename = shaderfile + FRAGMENT_POSTFIX;
	const std::string fragmentBuffer = filesystem()->load(fragmentFilename);
	if (fragmentBuffer.empty()) {
		Log::error("Could not load %s", fragmentFilename.c_str());
		_exitCode = 1;
		return core::AppState::Cleanup;
	}

	const std::string vertexFilename = shaderfile + VERTEX_POSTFIX;
	const std::string vertexBuffer = filesystem()->load(vertexFilename);
	if (vertexBuffer.empty()) {
		Log::error("Could not load %s", vertexFilename.c_str());
		_exitCode = 1;
		return core::AppState::Cleanup;
	}

	const std::string geometryFilename = shaderfile + GEOMETRY_POSTFIX;
	const std::string geometryBuffer = filesystem()->load(geometryFilename);

	video::Shader shader;
	const std::string& fragmentSrcSource = shader.getSource(video::ShaderType::Fragment, fragmentBuffer, false);
	const std::string& vertexSrcSource = shader.getSource(video::ShaderType::Vertex, vertexBuffer, false);

	_shaderStruct.filename = shaderfile;
	_shaderStruct.name = shaderfile;
	parse(fragmentSrcSource, false);
	if (!geometryBuffer.empty()) {
		const std::string& geometrySrcSource = shader.getSource(video::ShaderType::Geometry, geometryBuffer, false);
		parse(geometrySrcSource, false);
	}
	parse(vertexSrcSource, true);
	generateSrc();

	// set some cvars to led the validation work properly
	core::Var::get(cfg::ClientGamma, "2.2", core::CV_SHADER);
	core::Var::get(cfg::ClientDeferred, "false", core::CV_SHADER);
	core::Var::get(cfg::ClientShadowMap, "true", core::CV_SHADER);

	const std::string& fragmentSource = shader.getSource(video::ShaderType::Fragment, fragmentBuffer, true);
	const std::string& vertexSource = shader.getSource(video::ShaderType::Vertex, vertexBuffer, true);
	const std::string& geometrySource = shader.getSource(video::ShaderType::Geometry, geometryBuffer, true);

	Log::debug("Writing shader file %s to %s", shaderfile.c_str(), filesystem()->homePath().c_str());
	std::string finalFragmentFilename = _appname + "-" + fragmentFilename;
	std::string finalVertexFilename = _appname + "-" + vertexFilename;
	std::string finalGeometryFilename = _appname + "-" + geometryFilename;
	filesystem()->write(finalFragmentFilename, fragmentSource);
	filesystem()->write(finalVertexFilename, vertexSource);
	if (!geometrySource.empty()) {
		filesystem()->write(finalGeometryFilename, geometrySource);
	}

	Log::debug("Validating shader file %s", shaderfile.c_str());

	std::vector<std::string> fragmentArgs;
	fragmentArgs.push_back(filesystem()->homePath() + finalFragmentFilename);
	int fragmentValidationExitCode = core::Process::exec(glslangValidatorBin, fragmentArgs);

	std::vector<std::string> vertexArgs;
	vertexArgs.push_back(filesystem()->homePath() + finalVertexFilename);
	int vertexValidationExitCode = core::Process::exec(glslangValidatorBin, vertexArgs);

	int geometryValidationExitCode = 0;
	if (!geometrySource.empty()) {
		std::vector<std::string> geometryArgs;
		geometryArgs.push_back(filesystem()->homePath() + finalGeometryFilename);
		geometryValidationExitCode = core::Process::exec(glslangValidatorBin, geometryArgs);
	}

	if (fragmentValidationExitCode != 0) {
		_exitCode = fragmentValidationExitCode;
	} else if (vertexValidationExitCode != 0) {
		_exitCode = vertexValidationExitCode;
	} else if (geometryValidationExitCode != 0) {
		_exitCode = geometryValidationExitCode;
	}

	return core::AppState::Cleanup;
}

int main(int argc, char *argv[]) {
	const core::EventBusPtr eventBus = std::make_shared<core::EventBus>();
	const io::FilesystemPtr filesystem = std::make_shared<io::Filesystem>();
	ShaderTool app(filesystem, eventBus);
	return app.startMainLoop(argc, argv);
}
