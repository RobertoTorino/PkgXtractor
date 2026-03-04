// adapted from InstallDragDropPkg in main_window.cpp

#include <iostream>
#include <filesystem>
#include <sstream>
#include <vector>
#include <cstdint>
#include <algorithm>
#include "../../src/core/file_format/pkg.h"
#include "../../src/core/file_format/psf.h"
#include "../../src/common/path_util.h"
#include "../../src/common/string_util.h"
#include "../../src/core/loader.h"

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
		
		PKG pkg{};
		
		std::string failreason;
		if (!pkg.Extract(file, output_folder_path.empty() ? file.parent_path() : output_folder_path,
		                 failreason)) {
			std::cout << "Cannot open PKG file : " << failreason << std::endl;
		}else{
			std::cout << "open PKG file success" << std::endl;
			
			PSF psf{};
			
			if (!psf.Open(pkg.sfo)) {
				std::cout << "Could not read SFO." << std::endl;
			}else{
				std::cout << "Successfully read SFO." << std::endl;
				
				auto dlc_flag_data = psf.GetInteger("CONTENT_TYPE");
				
				if (output_folder_path == ""){
					output_folder_path = file.parent_path();
				}
				
				std::string folder_name = "";
				
				if(dlc_flag_data && dlc_flag_data.value() != 0){
					std::cout << "DLC detected.\n";
					auto title_id_data = psf.GetString("TITLE_ID");
					if (title_id_data) {
						folder_name = std::string(title_id_data.value());
						folder_name.erase(std::remove(folder_name.begin(), folder_name.end(), '\0'), folder_name.end());
						folder_name = "[DLC] " + folder_name;
					}
				} else{
					std::cout << "Game or Patch detected.\n";
					auto title_data = psf.GetString("TITLE");
					if (title_data) {
						folder_name = std::string(title_data.value());
						folder_name.erase(std::remove(folder_name.begin(), folder_name.end(), '\0'), folder_name.end());
					}
				}
				
				if (!folder_name.empty()) {
					output_folder_path = output_folder_path / folder_name;
				}

				std::cout << "Game/DLC folder will be extracted to: " << output_folder_path.string()
				          << std::endl;

				const u32 files_count = pkg.GetNumberOfFiles();
				for (u32 index = 0; index < files_count; ++index) {
					pkg.ExtractFiles(static_cast<int>(index));
				}
				
				std::cout << "Extraction complete!\n" << std::endl;
			}
		}
	}else{
		std::cout << file << " is not a valid PKG file" << std::endl;
	}
	
	return 0;
}
