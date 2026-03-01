// adapted from InstallDragDropPkg in main_window.cpp

#include <iostream>
#include <filesystem>
#include <sstream>
#include <vector>
#include <cstdint>
#include "../../src/core/file_format/pkg.h"
#include "../../src/core/file_format/psf.h"
#include "../../src/common/path_util.h"
#include "../../src/common/string_util.h"
#include "../../src/core/loader.h"

using PSF = Psf;
using PkgFile = ::Pkg::Pkg;
using LoaderClass = Loader;

std::vector<std::string> SplitString(const std::string& str, char delimiter) {
	std::vector<std::string> tokens;
	std::stringstream ss(str);
	std::string token;
	while (std::getline(ss, token, delimiter)) {
		tokens.push_back(token);
	}
	return tokens;
}

int main(int argc, char** argv){
	std::filesystem::path file = "";
	std::filesystem::path output_folder_path = "";
	
	if(argc > 1){
		file += argv[1];
		
		if(argc > 2){
			output_folder_path += argv[2];
		}
	}
	
	if (Loader::DetectFileType(file) == Loader::FileTypes::Pkg) {
		std::cout << file << " is a valid PKG\n" << std::endl;
		
		PkgFile pkg = PkgFile();
		
		std::string failreason;
		if (!pkg.Open(file, failreason)) {
			std::cout << "Cannot open PKG file : " << failreason << std::endl;
		}else{
			std::cout << "open PKG file success" << std::endl;
			
			PSF psf = PSF();
			
			if (!psf.Open(pkg.sfo)) {
				std::cout << "Could not read SFO." << std::endl;
			}else{
				std::cout << "Successfully read SFO." << std::endl;
				
				auto dlc_flag = psf.GetEntry(0x20 - 1);
				
				if (output_folder_path == ""){
					output_folder_path = file.parent_path();
				}
				
				std::string folder_name = "";
				
				if(dlc_flag != nullptr && *(uint32_t*)dlc_flag->value.data() != 0){
					std::cout << "DLC detected.\n";
					auto title_id = psf.GetEntry(0x22 - 1);
					if (title_id != nullptr) {
						folder_name = std::string((char*)title_id->value.data(), title_id->value.size());
						folder_name.erase(std::remove(folder_name.begin(), folder_name.end(), '\0'), folder_name.end());
						folder_name = "[DLC] " + folder_name;
					}
				} else{
					std::cout << "Game or Patch detected.\n";
					auto title = psf.GetEntry(0x23 - 1);
					if (title != nullptr) {
						folder_name = std::string((char*)title->value.data(), title->value.size());
						folder_name.erase(std::remove(folder_name.begin(), folder_name.end(), '\0'), folder_name.end());
					}
				}
				
				output_folder_path = output_folder_path / folder_name;
				
				std::cout << "Game/DLC folder will be extracted to: " << output_folder_path.string() << std::endl;
				
				for (auto& file : pkg.GetFiles()) {
					std::cout << "Extracting: " << file.path << std::endl;
					
					if (!pkg.Extract(file, output_folder_path)) {
						std::cout << "Error extracting " << file.path << std::endl;
					}
				}
				
				std::cout << "Extraction complete!\n" << std::endl;
			}
		}
	}else{
		std::cout << file << " is not a valid PKG file" << std::endl;
	}
	
	return 0;
}
