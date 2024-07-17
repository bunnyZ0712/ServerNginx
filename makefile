#项目整体入口，编译起总体作用

include config.mk
all:
#-C是指定目录
#make -C signal   

#可执行文件应该放最后
#make -C app      

#用shell命令for搞，shell里边的变量用两个$ make -c告诉makefile的路径
	@for dir in $(BUILD_DIR); \
	do \
		make -C $$dir; \
	done


clean:
#-rf：删除文件夹，强制删除
	rm -rf app/link_obj app/dep nginx
	rm -rf signal/*.gch app/*.gch