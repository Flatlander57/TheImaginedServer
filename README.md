TheImaginedServer
=================

### What is The Imagined Server?

"The Imagined Server" is a version of "The Forgotten Server" customized and made to work with the OTClient. The goal is to provide any feature you can imagine that would add to the functionality of the server, and make it easier to use and customize.

### Why should I use The Imagined Server?

Most of you reading this are probably from the OpenTibia community. You might have noticed that most servers that are released are very similar. They all have the same features, same sprites, and sometimes even the same map.
If you are looking to make a server, and you want the *freedom* to make something different, new, and exciting. Then "The Imagined Server" is for you. 

### Features

The plan is to literally add ANYTHING that will increase the functionality of TFS. If you can imagine a feature, and it is possible, we will add it.

### Compiling

I personally only compile using Microsoft Visual Studio (It is free, and works great)
* [Microsoft Visual Studio 2013] (http://www.microsoft.com/en-us/download/details.aspx?id=40787)

You'll also need TheForgottenServer's SDK 1.0, Boost Libarries, and TIS-Source.
* [TFS-SDK-1.0.rar] (https://www.dropbox.com/s/bjhknzzrizs4cgq/tfs-sdk-1.0.rar)
* [Boost 32 bit download] (http://boost.teeks99.com/bin/1.55.0/boost_1_55_0-msvc-12.0-32.exe) or  [Boost 64 bit download] (http://boost.teeks99.com/bin/1.55.0/boost_1_55_0-msvc-12.0-64.exe)
* [TheImaginedServer-Source] (https://github.com/Flatlander57/TheImaginedServer/archive/master.zip)

Step By Step Compiling:
* Open **tfs.vcxproj** (VC++ Project) since the VC12 folder in Microsoft Visual Studio Express 2013.
* Go to **Project->Properties** (Or press Alt+F7)
* **Click on Configuration Manager**, then select **Release** and either **win32 or x64** (depending on what OS you want to use to hose the server) then **click close**.
* Under **Configeration Properties->General** change the **Platform Toolset** to **Visual Studio 2012 (v110)**
* Under **C/C++->General** open the **Additional Include Directories** window.
* You will need to add the following include directories:

  `\VC\boost_1_53_0\boost`  
  `\VC\boost_1_53_0`    
  `\VC\VC\include`    
  `\VC\VC\`    
  `\tfs-sdk-1.0\zlib-1.2.5\include`     
  `\tfs-sdk-1.0\sqlite3-3.7.7.1\include`      
  `\tfs-sdk-1.0\postgresql-9.1.2-1\include`    
  `\tfs-sdk-1.0\openssl-0.9.8k\include`    
  `\tfs-sdk-1.0\mysql-connector-c-6.0.2\include`      
  `\tfs-sdk-1.0\mysql-connector-c++-1.1.0\include`      
  `\tfs-sdk-1.0\mpir-2.5.0\include`    
  `\tfs-sdk-1.0\LuaJIT-2.0.0-beta9\include`     
  `\tfs-sdk-1.0\libxml2-2.7.8\include`      
  `\tfs-sdk-1.0\libiconv-1.14\include`      
* Under **Linker->General** open the **Additional Library Directories** window.
* You will need to add the following lib directories: **If you selected win32 then select the normal Lib instead of lib64**
  `\VC\library64`   
  `\VC\lib64`   
  `\VC\boost`   
  `\VC\boost_1_53_0\boost`    
  `\VC\boost_1_53_0`    
  `\tfs-sdk-1.0\zlib-1.2.5\lib64`   
  `\tfs-sdk-1.0\sqlite3-3.7.7.1\lib64`    
  `\tfs-sdk-1.0\postgresql-9.1.2-1\lib64`   
  `\tfs-sdk-1.0\openssl-0.9.8k\lib64`   
  `\tfs-sdk-1.0\mysql-connector-c-6.0.2\lib64`    
  `\tfs-sdk-1.0\mysql-connector-c++-1.1.0\lib64`    
  `\tfs-sdk-1.0\mpir-2.5.0\lib64`   
  `\tfs-sdk-1.0\LuaJIT-2.0.0-beta9\lib64`   
  `\tfs-sdk-1.0\libxml2-2.7.8\lib64`    
  `\tfs-sdk-1.0\libiconv-1.14\lib64`    

* Now click **Apply** then **OK** and wait for all the files to parse. (It will say Ready at the bottom of the screen when done)
* Once it is ready go to **Build->Rebuild Solution** (or press Ctrl+Alt+F7)
* It should rebuild correctly (I hope... I made this mostly from Memory)
