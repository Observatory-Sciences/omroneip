#include "drvOmroneip.h"
    
omronUtilities::omronUtilities(drvOmronEIP *ptrDriver)
{
    pDriver = ptrDriver;
    pasynUserSelf = pDriver->pasynUserSelf;
    driverName = pDriver->driverName;
}

drvInfoMap omronUtilities::drvInfoParser(const char *drvInfo)
{
  const char * functionName = "drvInfoParser";
  const std::string str(drvInfo);
  char delim = ' ';
  char escape = '/'; // I added support for tagNames which include spaces if inside the escape char, however the PLC does not support 
  // this and it is not well tested. You should also be able to define a poller with a space in it, if you wanted to for some reason
  drvInfoMap keyWords = {
      {"pollerName", "none"}, // optional
      {"tagName", "none"},  
      {"dataType", "none"}, 
      {"startIndex", "1"},   
      {"sliceSize", "1"},     
      {"offset", "0"},       
      {"tagExtras", "none"},
      {"strCapacity", "0"}, // only needed for getting strings from UDTs  
      {"optimisationFlag", "not requested"}, // stores the status of optimisation, ("not requested", "attempt optimisation","dont optimise","optimisation failed","optimised","master")
      {"stringValid", "true"}, // set to false if errors are detected which aborts creation of tag and asyn parameter, return early if false
      {"offsetReadSize", "0"},
      {"readAsString", "0"}, // currently just used to optionally output the TIME dtypes as user friendly strings in the local timezone
      {"optimise", "0"} // if 0 then we use the offset to look within a datatype, if 1 then we use it to get a datatype from within an array/UDT
  };
  std::list<std::string> words; // Contains a list of string parameters supplied by the user through a record's drvInfo interface.
  std::string substring;
  bool escaped = false;
  size_t pos = 0;

  for (size_t i = 0; i < str.size(); i++)
  {
    if (str[i] == escape)
    {
      if (escaped == false)
      {
        escaped = true;
      }
      else
      {
        escaped = false;
      }
    }
    if (str[i] == delim && !escaped)
    {
      if (i == 0)
      {
        pos += 1;
        continue;
      }
      else if (str[i - 1] == escape)
      {
        // delimeter after escape character
        substring = str.substr(pos + 1, (i - 1) - (pos + 1));
      }
      // regular delimeter
      else
      {
        substring = str.substr(pos, i - pos);
      }
      words.push_back(substring);
      pos = i + 1;
    }
    else if (i == str.size() - 1)
    {
      substring = str.substr(pos, str.size() - pos);
      words.push_back(substring);
    }
  }

  if (escaped)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Escape character never closed. Record invalid\n", driverName, functionName);
    keyWords.at("stringValid") = "false";
  }

  if (words.size() < 1)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, No arguments supplied to driver\n", driverName, functionName);
    keyWords.at("stringValid") = "false";
  }

  if (words.front()[0] == '@')
  {
    // Sort out potential readpoller reference
    if (pDriver->pollerList_.find(words.front().substr(1)) != pDriver->pollerList_.end()) // check if poller exists
    {
      keyWords.at("pollerName") = words.front().substr(1);
      words.pop_front();
    }
    else
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, the named poller: %s does not exist!\n", driverName, functionName, words.front().c_str());
      keyWords.at("stringValid") = "false";
      return keyWords;
    }
  }

  if (words.size() < 5)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Record is missing parameters. Expected 5 space seperated terms (or 6 including poller) but recieved: %ld\n", driverName, functionName, words.size());
    keyWords.at("stringValid") = "false";
    return keyWords;
  }

  int params = words.size();
  bool indexable = false;
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "============================================================================================\n");
  for (int i = 0; i < params; i++)
  {
    const std::string thisWord = words.front(); // The word which we are currently processing to extract the required information
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Processing drvInfo parameter: %s\n", driverName, functionName, words.front().c_str());
    if (i == 0)
    {
      // Check for valid name or name[startIndex]
      std::string startIndex;
      auto b = thisWord.begin(), e = thisWord.end();

      while ((b = std::find(b, e, '[')) != e)
      {
        auto n_start = ++b;
        b = std::find(b, e, ']');
        startIndex = std::string(n_start, b);
        if (!startIndex.empty())
        {
          break;
        }
      }
      if (!startIndex.empty())
      {
        keyWords.at("startIndex") = startIndex;
        indexable = true;
        try
        {                
          if (std::stoi(startIndex) < 1)
          {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, A startIndex of < 1 is forbidden\n", driverName, functionName);
            keyWords.at("stringValid") = "false";
            return keyWords;
          }
        }
        catch(...)
        {
          asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, startIndex must be an integer.\n", driverName, functionName);
          keyWords.at("stringValid") = "false";
          return keyWords;
        }
      }
      keyWords.at("tagName") = thisWord;
      words.pop_front();
    }
    else if (i == 1)
    {
      // Checking for valid datatype
      bool validDataType = false;
      for (size_t t = 0; t < pDriver->omronDataTypeList.size(); t++)
      {
        if (thisWord == pDriver->omronDataTypeList[t].first)
        {
          validDataType = true;
          keyWords.at("dataType") = thisWord;
        }
      }
      if (!validDataType)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Datatype invalid.\n", driverName, functionName);
        keyWords.at("stringValid") = "false";
        return keyWords;
      }
      words.pop_front();
    }
    else if (i == 2)
    {
      // Checking for valid sliceSize
      char *p;
      if (thisWord == "none"){
        keyWords.at("sliceSize") = "1";
      }
      else {
        strtol(thisWord.c_str(), &p, 10);
        if (*p == 0)
        {
          if (indexable && thisWord != "1")
          {
            keyWords.at("sliceSize") = thisWord;
            if (keyWords.at("dataType")=="STRING" || keyWords.at("dataType")=="LINT" || keyWords.at("dataType")=="ULINT"){
              asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Error! sliceSize must be 1 for this datatype.\n", driverName, functionName);
              keyWords.at("stringValid") = "false";
              return keyWords;
            }
          }
          else if (thisWord == "0")
          {
            keyWords.at("sliceSize") = "1";
          }
          else if (thisWord != "1")
          {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s You cannot get a slice of a whole tag. Try tag_name[startIndex] to specify elements for slice.\n", driverName, functionName);
            keyWords.at("stringValid") = "false";
            return keyWords;
          }
        }
        else
        {
          asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid sliceSize, must be integer.\n", driverName, functionName);
          keyWords.at("stringValid") = "false";
          return keyWords;
        }
      }
      words.pop_front();
    }
    else if (i == 3)
    {
      // Checking for valid offset, either a positive integer or a reference to a structs definition file, eg structName[2][11]...
      size_t indexStartPos = 0; // stores the position of the first '[' within the user supplied string
      int offset;
      std::vector<size_t> structIndices; // the indice(s) within the structure specified by the user
      bool indexFound = false;
      bool firstIndex = true;
      // user has chosen not to use an offset
      if (thisWord == "none")
      {
        keyWords.at("offset") = "0";
      }
      else {
        // attempt to set offset to integer, if not possible then assume it is a structname
        try
        {
          offset = std::stoi(thisWord);
        }
        catch(...)
        {
          // It is either a syntax error, or a reference to a structure, we attempt to split the name and integer
          for (size_t n = 0; n<thisWord.size(); n++)
          {
            if (thisWord.c_str()[n] == '[') // check each character of the word until we have found an opening bracket
            {
              std::string offsetSubstring = thisWord.substr(n+1);
              if (firstIndex) {indexStartPos = n;} //only want to update indexStartPos, once we have already found the first index
              for (size_t m = 0; m<offsetSubstring.size(); m++)
              {
                if (offsetSubstring.c_str()[m] == ']') // check each character of the word until we have found a closing bracket
                {
                  try
                  {
                    // struct integer found
                    // try to convert the string between the brackets to an int, we also -1 to convert from the user input which numbers from 1
                    // to the the system used to get the offset which numbers from 0
                    structIndices.push_back(std::stoi(offsetSubstring.substr(0,m))-1);
                    indexFound = true;
                    firstIndex = false;
                  }
                  catch(...){
                    indexFound = false;
                  }
                  break;
                }
              }
            }
          }
          if (!indexFound){
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Could not find a valid index for the requested structure: %s\n", driverName, functionName, words.front().c_str());
            keyWords.at("stringValid") = "false";
            return keyWords;
          }
          
          //look for matching structure in structMap_
          //if found, look for the offset at the structIndex within the structure
          std::string structName = thisWord.substr(0,indexStartPos);
          bool structFound = false;
          for (auto item: pDriver->structMap_)
          {
            if (item.first == structName)
            {
              //requested structure found
              structFound = true;
              offset = findRequestedOffset(structIndices, structName); //lookup byte offset based off user supplied indice(s)
              if (offset >=0) {}
              else {
                offset=0;
                asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid index or structure name: %s\n", driverName, functionName, thisWord.c_str());
                keyWords.at("stringValid") = "false";
                return keyWords;
              }
              break;
            }
          }
          if (!structFound)
          {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Could not find structure requested: %s. Have you loaded a struct file?\n", driverName, functionName, thisWord.c_str());
            keyWords.at("stringValid") = "false";
            return keyWords;
            offset=0;
          }
        }
        keyWords.at("offset") = std::to_string(offset);
      }
      words.pop_front();
    }
    else if (i == 4)
    {
      // Check for valid extra attributes
      /* These attributes overwrite libplctag attributes, other attributes which arent overwritten are not mentioned here
        Users can overwrite these defaults and other libplctag defaults from their records */
      drvInfoMap defaultTagAttribs = {
          {"allow_packing=", "1"},
          {"str_is_zero_terminated=", "0"},
          {"str_is_fixed_length=", "0"},
          {"str_is_counted=", "1"},
          {"str_count_word_bytes=", "2"},
          {"str_pad_to_multiple_bytes=", "0"}};

      std::string extrasString;
      std::string extrasWord;
      if ((thisWord!="0" && thisWord!="none") || keyWords.at("dataType")=="STRING") 
      {
        // The user has specified attributes other than default, these will either be added to the list or replace existing default values
        if ((keyWords.at("dataType")=="STRING")&&(thisWord=="0" || thisWord=="none")){
          //We have a string dtype which has not defined its extrasString, this is improper behaviour but we allow it here and create
          //a warning later. We must strip away the none identifier.
          extrasString="",extrasWord="";
        }
        else
          extrasString = thisWord, extrasWord = thisWord;

        // look for and process the strings: str_max_capacity, offset_read_size and as_time
        processExtrasExceptions(thisWord, keyWords, extrasString, defaultTagAttribs);

        for (auto &attrib : defaultTagAttribs)
        {
          auto pos = extrasWord.find(attrib.first);
          std::string size;
          if (pos != std::string::npos) // if attrib is one of our defined defaults
          {
            std::string remaining = extrasWord.substr(pos + attrib.first.size(), extrasWord.size());
            auto nextPos = remaining.find('&');
            if (nextPos != std::string::npos)
            {
              size = remaining.substr(0, nextPos);
              extrasString = extrasWord.erase(pos-1, attrib.first.size() + nextPos + 1);
            }
            else if (pos!=0)
            {
              size = remaining.substr(0, remaining.size());
              extrasString = extrasWord.erase(pos-1, extrasWord.size()-(pos-1));
            }
            else { //user has forgotten to add & at the start
              size = remaining.substr(0, remaining.size()); 
              extrasString = extrasWord.erase(pos, extrasWord.size()-pos);
            }

            if (size == attrib.second) // if defined value is identical to default, continue
            {
              continue;
            }
            else // set new default value
            {
              attrib.second = size;
            }
          }
        }
        if (keyWords.at("stringValid") == "false"){
          return keyWords;
        }
      }

      //Add the non default tag attributes to the map
      for (auto attrib : defaultTagAttribs)
      {
        if (attrib.first.substr(0,3) == "str" && !(keyWords.at("dataType") == "STRING" || keyWords.at("dataType") == "UDT"))
        {
          // If the user requests str type attributes for a non STRING record then ignore
          continue;
        }
        extrasString += "&";
        extrasString += attrib.first;
        extrasString += attrib.second;
      }
      keyWords.at("tagExtras") = extrasString;
      words.pop_back();
    }
  }

  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Creating libplctag tag with the following parameters:\n", driverName, functionName);
  for (auto i = keyWords.begin(); i != keyWords.end(); i++)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s = %s\n", i->first.c_str(), i->second.c_str());
  }
  return keyWords;
}

void omronUtilities::processExtrasExceptions(std::string thisWord, drvInfoMap &keyWords, std::string &extrasString, drvInfoMap &defaultTagAttribs)
{
  static const char *functionName = "processExtrasExceptions";
  std::string size;

  // we check to see if offset_read_size is defined. This is a bit different as it is not used in libplctag, so is removed from extrasString
  size_t pos = thisWord.find("offset_read_size=");
  if (pos != std::string::npos)
  {
    if (keyWords.at("dataType")=="UDT" || keyWords.at("dataType")=="STRING"){
      std::string remaining = thisWord.substr(pos + sizeof("offset_read_size=")-1, thisWord.size());
      auto nextPos = remaining.find('&');
      if (nextPos != std::string::npos)
      {
        size = remaining.substr(0, nextPos);
      }
      else
      {
        size = remaining.substr(0, remaining.size());
      }

      try
      {
        //offset_read_size found
        std::stoi(size); // try to convert the string to an int
        keyWords.at("offsetReadSize") = size;  
        extrasString.erase(pos, nextPos-pos);
      }
      catch(...){
        keyWords.at("stringValid") = "false";
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid integer for offset_read_size: %s\n", driverName, functionName, size.c_str());
      }
    }
    else {
      asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, offset_read_size should only be set for UDT type.\n", 
                  driverName, functionName);
    }
  }

  // we check to see if read_as_string is defined. This is a bit different as it is not used in libplctag, so is removed from extrasString
  pos = thisWord.find("read_as_string=");
  if (pos != std::string::npos)
  {
    if (keyWords.at("dataType")=="TIME"){
      std::string remaining = thisWord.substr(pos + sizeof("read_as_string=")-1, thisWord.size());
      auto nextPos = remaining.find('&');
      if (nextPos != std::string::npos)
      {
        size = remaining.substr(0, nextPos);
      }
      else
      {
        size = remaining.substr(0, remaining.size());
      }

      try
      {
        //read_as_string found
        std::stoi(size); // try to convert the string to an int
        keyWords.at("readAsString") = size;  
        extrasString.erase(pos, nextPos-pos);
      }
      catch(...){
        keyWords.at("stringValid") = "false";
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid value for as_string=: %s\n", driverName, functionName, size.c_str());
      }
    }
    else {
      asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, read_as_string= should only be set for TIME type.\n", 
                  driverName, functionName);
    }
  }

  // we check to see if &optimise= is defined. This is a bit different as it is not used in libplctag, so is removed from extrasString
  pos = thisWord.find("optimise=");
  if (pos != std::string::npos)
  {
    std::string remaining = thisWord.substr(pos + sizeof("optimise=")-1, thisWord.size());
    auto nextPos = remaining.find('&');
    if (nextPos != std::string::npos)
    {
      size = remaining.substr(0, nextPos);
    }
    else
    {
      size = remaining.substr(0, remaining.size());
    }

    try
    {
      //optimise= found
      std::stoi(size); // try to convert the string to an int
      keyWords.at("optimise") = size;  
      keyWords.at("optimisationFlag") = "attempt optimisation";
      defaultTagAttribs = { //we need different defaults if doing optimisation
          {"allow_packing=", "1"},
          {"str_is_zero_terminated=", "1"},
          {"str_is_fixed_length=", "0"},
          {"str_is_counted=", "0"},
          {"str_count_word_bytes=", "0"},
          {"str_pad_to_multiple_bytes=", "0"}};
      extrasString.erase(pos, nextPos-pos);
    }
    catch(...){
      keyWords.at("stringValid") = "false";
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid value for optimise=: %s\n", driverName, functionName, size.c_str());
    }
  }

  // we check to see if str_max_capacity is set, this is needed to get strings from UDTs
  pos = thisWord.find("str_max_capacity=");
  if (keyWords.at("dataType")=="STRING"){
    if (pos != std::string::npos)
    {
      std::string remaining = thisWord.substr(pos + sizeof("str_max_capacity=")-1, thisWord.size());
      auto nextPos = remaining.find('&');
      if (nextPos != std::string::npos)
      {
        size = remaining.substr(0, nextPos);
      }
      else
      {
        size = remaining.substr(0, remaining.size());
      }
      try
      {
        //str_max_capacity found
        std::stoi(size); // try to convert the string to an int
        keyWords.at("strCapacity") = size;  
      }
      catch(...){
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid integer for str_max_capacity: %s\n", driverName, functionName, size.c_str());
        keyWords.at("stringValid") = "false";
      }
    }
    else if (keyWords.at("optimise")=="0" && keyWords.at("offset") == "0"){
      asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, str_max_capacity has not been defined, this can cause errors when reading strings from structures and when writing strings. This should be defined.\n", 
                  driverName, functionName);
    }
    else {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, str_max_capacity has not been defined, this must be defined when attempting optimisations or using offsets!\n", 
            driverName, functionName);
      keyWords.at("stringValid") = "false";
    }
  }
}

int omronUtilities::findRequestedOffset(std::vector<size_t> indices, std::string structName)
{
  static const char *functionName = "findRequestedOffset";
  size_t j = 0; // The element within structDtypeMap (this map includes start: and end: tags)
  size_t m = 0; // Element within structMap, stores the offset values and no start: or end: tags
  size_t i = 0; // User supplied element, essentially it keeps count within one "level" of the struct, each index within the indices vector is on a different "level"
  size_t n = 0; // Used to count through arrays to work out how big they are
  size_t k = 0; // Used to count through structs to work out how big they are
  size_t currentIndex = 0; // Used to keep track of which index we are currently on
  int offset; // The offset found from structMap based off user requested indices.
  std::string dtype;
  std::vector<std::string> dtypeRow; // check for error
  std::stringstream indicesPrintString;
  try {
    dtypeRow = pDriver->structDtypeMap_.at(structName);
  }
  catch (...) {
    return -1;
  }

  std::copy(indices.begin(), indices.end(), std::ostream_iterator<int>(indicesPrintString, " "));
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Finding offset for struct: %s at the indices (numbered from 0): %s\n", driverName, functionName, structName.c_str(), indicesPrintString.str().c_str());

  for (size_t index : indices){
    currentIndex++;
    for (i = 0; i<=index; i++){
      if (i==index){
        j++; //if we have more than 1 index, we must have a start:structName or start:array, this skips that item
        if (currentIndex == indices.size()-1 && dtypeRow[j].substr(0,6) == "start:"){
          /* if we are in the deepest layer (l== indices.size-1) and the current dtype would be the start of that layer, 
            we must skip the tag so that we are processing the raw dtypes within it */
          j++; 
        }
        break;
      }
      
      if (j < dtypeRow.size()){
        dtype = dtypeRow[j];
      }
      else {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid index: %ld for structure: %s\n", driverName, functionName, index, structName.c_str());
        return -1; 
      }

      if (dtype=="SINT" || dtype=="USINT" ||dtype =="UINT" || dtype =="INT" || dtype =="WORD" || dtype =="BOOL" || 
            dtype =="UDINT" || dtype =="DINT" || dtype =="REAL" || dtype =="DWORD" || dtype =="ULINT" || dtype =="LINT" || 
              dtype == "LREAL" || dtype =="LWORD" || dtype =="TIME" || dtype.substr(0,7)=="STRING["){
        j++;m++;
      }
      else if ((pDriver->structMap_.find(dtype.substr(6)))!=pDriver->structMap_.end()){
        // look for the next instance of end:structMap and calculate the number of raw dtypes between it and start:structMap
        size_t structDtypes = 0;
        k=j+1; // set k to the first element after start:structMap
        while (dtypeRow[k]!="end:"+dtypeRow[j].substr(6))
        {
          dtype = dtypeRow[k];
          if (dtype=="SINT" || dtype=="USINT" ||dtype =="UINT" || dtype =="INT" || dtype =="WORD" || dtype =="BOOL" || 
                dtype =="UDINT" || dtype =="DINT" || dtype =="REAL" || dtype =="DWORD" || dtype =="ULINT" || dtype =="LINT" || 
                  dtype == "LREAL" || dtype =="LWORD" || dtype =="TIME" || dtype.substr(0,7)=="STRING["){
            structDtypes++;
          }
          k++;
          if (k>=dtypeRow.size()){
            break;
          }
        }
        j=k; // j should now be set to the dtype after the end:structName tag
        m+=structDtypes;
      }
      else if (dtype=="start:array"){
        // if this dtype is the start of an array, we get the number of dtypes in the array and skip over these
        for (n = j; n<dtypeRow.size(); n++){
          if (dtypeRow[n]=="end:array"){
            break;
          }
        }
        m += n-j-1; //we add just the number of array elements
        j = n+1; //we add the number of array elements plus start: and end: tags
        n=0;
      }
      else if (dtype.substr(0,4)=="end:"){
        j++;
        i--;
      }
      else {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Failed to calculate offset due to invalid datatype: %s\n", driverName, functionName, dtype.c_str());
        return -1;
      }
    }
  }
  offset = pDriver->structMap_.at(structName)[m];
  return offset;
}

asynStatus omronUtilities::createStructMap(structDtypeMap rawMap)
{
  const char * functionName = "createStructMap";
  size_t status = asynSuccess;
  std::unordered_map<std::string, std::vector<int>> structMap;
  structDtypeMap expandedMap = rawMap;
  pDriver->structRawMap_ = rawMap;
  for (auto& kv: expandedMap)
  {// expand embedded structures so that the structure just contains standard dtypes
    kv.second = expandStructsRecursive(expandedMap, kv.first);
    if (std::find(kv.second.begin(), kv.second.end(), "Invalid") != kv.second.end()){
      //If either expandStructsRecursive or expandArrayRecursive returned "Invalid" then return asynError
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Encountered an error while expanding struct: %s\n", driverName, functionName, kv.first.c_str());
      return asynError;
    }
  }

  pDriver->structDtypeMap_ = expandedMap;

  std::string flowString;
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Struct defs with expanded embedded structs and arrays:\n", driverName, functionName);
  for (auto const& i : expandedMap) {
    flowString += i.first + ": ";
    for (auto const& j : i.second)
      flowString += j + " ";
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s\n", flowString.c_str());
    flowString.clear();
  }
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "\n");

  for (auto kv: rawMap)
  {// look for kv.first in structMap through a recursive search
    status = findOffsets(expandedMap, kv.first, structMap);
  }

  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s The processed struct definitions with their calculated offsets are:\n", driverName, functionName);
  for (auto const& i : structMap) {
    flowString += i.first + ": ";
    for (auto const& j : i.second)
      flowString += std::to_string(j) + " ";
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s\n", flowString.c_str());
    flowString.clear();
  }
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "\n");

  pDriver->structMap_= structMap;
  if (status < 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, an error occured while calculating the offsets within the loaded struct file\n", driverName, functionName);
    return asynError;
  }
  return asynSuccess;
}

std::vector<std::string> omronUtilities::expandArrayRecursive(structDtypeMap const& rawMap, std::string arrayDesc)
{
  static const char *functionName = "expandArrayRecursive";
  // "ARRAY[x..y] OF z"
  std::vector<std::string> expandedData;
  std::string ss = arrayDesc.substr(7,arrayDesc.size()-7);
  int arrayLength = 0;
  bool dimsFound = false;
  std::string dtype;

  // Get the size of the array
  for (size_t i = 0; i<ss.size(); i++)
  {
    if (ss.substr(i,1)== "]")
    {
      // Get the x..y part and find x and y in order to get the array size
      std::string arrayDims = ss.substr(0, i);
      std::string arrayStartString = arrayDims.substr(0, arrayDims.find(".."));
      std::string arrayEndString = arrayDims.substr(arrayDims.find("..")+2);
      try
      {
        int arrayStart = std::stoi(arrayStartString);
        int arrayEnd = std::stoi(arrayEndString);
        arrayLength = arrayEnd-arrayStart+1;
        if (arrayStart < 0 || arrayEnd < 0 || arrayLength < 0) throw -1;
        dimsFound = true;
      }
      catch (...)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, array dimensions are invalid.\n", driverName, functionName);
      }
      break;
    }
  }
  if (!dimsFound)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, ARRAY type must be of the following format: \"ARRAY[x..y] OF z\", definition: %s is invalid\n", driverName, functionName, arrayDesc.c_str());
    expandedData.push_back("Invalid");
    return expandedData;
  }
  
  // Get the datatype of the array
  dtype = ss.substr(ss.find_last_of(' ')+1, ss.size()-(ss.find_last_of(' ')+1)-1); // We dont want the closing "
  std::vector<std::string> singleExpandedData;
  if (dtype=="SINT" || dtype=="USINT" ||dtype =="UINT" || dtype =="INT" || dtype =="WORD" || dtype =="BOOL" || 
        dtype =="UDINT" || dtype =="DINT" || dtype =="REAL" || dtype =="DWORD" || dtype =="ULINT" || dtype =="LINT" || 
          dtype == "LREAL" || dtype =="LWORD" || dtype =="TIME" || dtype.substr(0,7)=="STRING[") 
  {
    singleExpandedData.push_back(dtype);
  }
  else
  {
    // We assume that we have an array of structs and so we call the expandStructsRecursive function to get the raw dtypes from the struct
    std::vector<std::string> embeddedDtypes = expandStructsRecursive(rawMap, dtype);
    singleExpandedData.push_back("start:" + dtype);
    singleExpandedData.insert(std::end(singleExpandedData), std::begin(embeddedDtypes), std::end(embeddedDtypes));
    singleExpandedData.push_back("end:"+dtype);
  }

  for (size_t i = 0; i<(size_t)arrayLength; i++) {
    expandedData.insert(std::end(expandedData), std::begin(singleExpandedData), std::end(singleExpandedData));
  }
  return expandedData;
}

std::vector<std::string> omronUtilities::expandStructsRecursive(structDtypeMap const& rawMap, std::string structName)
{
  static const char *functionName = "expandStructsRecursive";
  std::vector<std::string> expandedRow;
  if (rawMap.find(structName)==rawMap.end()){
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Failed to find definition for struct: %s. \n", driverName, functionName, structName.c_str());
    expandedRow.push_back("Invalid");
    return expandedRow;
  }
  std::vector<std::string> row = rawMap.at(structName);
  const std::string arrayIdentifier = "\"ARRAY[";
  for (std::string dtype : row)
  {
    if (dtype=="SINT" || dtype=="USINT" ||dtype =="UINT" || dtype =="INT" || dtype =="WORD" || dtype =="BOOL" || 
          dtype =="UDINT" || dtype =="DINT" || dtype =="REAL" || dtype =="DWORD" || dtype =="ULINT" || dtype =="LINT" || 
            dtype == "LREAL" || dtype =="LWORD" || dtype =="TIME" || dtype.substr(0,7)=="STRING[")
    {
      expandedRow.push_back(dtype);
    }
    else if (dtype.substr(0,7)==arrayIdentifier)
    {
      // Expand array and add "start:array" and "end:array" either side
      std::vector<std::string> arrayDtypes = expandArrayRecursive(rawMap, dtype);
      expandedRow.push_back("start:array");
      expandedRow.insert(std::end(expandedRow), std::begin(arrayDtypes), std::end(arrayDtypes));
      expandedRow.push_back("end:array");
    }
    else if (dtype.substr(0,6)=="start:" || dtype.substr(0,4)=="end:")
    {
      // The struct we are looking through has already been expanded and this is a tag denoting the struct, therefore we just copy it in
      expandedRow.push_back(dtype);
    }
    else
    {
      // If we get here, then we assume that we have a structure, but it could be a typo
      try
      {
        // Recursively call this function to get all of the embedded datatypes
        // Before each embedded struct we add the struct name and after we add "end", this is so we know when to apply extra padding required for embedded structs
        std::vector<std::string> embeddedDtypes = expandStructsRecursive(rawMap, dtype);
        expandedRow.push_back("start:"+dtype);
        expandedRow.insert(std::end(expandedRow), std::begin(embeddedDtypes), std::end(embeddedDtypes));
        expandedRow.push_back("end:"+dtype);
      }
      catch (...)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Failed to find the standard datatype: %s. Definition for %s and its dependents failed.\n", driverName, functionName, dtype.c_str(), structName.c_str());
        expandedRow.push_back("Invalid");
        return expandedRow;
      }
    }
  }
  return expandedRow;
}

std::string omronUtilities::findArrayDtype(structDtypeMap const& expandedMap, std::string arrayDesc)
{
  static const char *functionName = "findArrayDtype";
  std::list<std::string> dtypeSet = {"SINT","USINT","INT","DINT","LINT","UINT","UDINT","ULINT","REAL","LREAL","STRING","WORD","DWORD","LWORD","TIME"};
  std::string dtypeData = arrayDesc.substr(arrayDesc.find("] OF ")+5, arrayDesc.size()-(arrayDesc.find("] OF ")+5));
  for (std::string dtype : dtypeSet) {
    // Check to see if array contains a standard dtype
    if (dtypeData.find(dtype) != std::string::npos){
      return dtype;
    }
  }
  // Check to see if array contains a valid struct
  if (expandedMap.find(dtypeData)!=expandedMap.end()){
    return dtypeData;
  }

  // If we get here then we have an invalid array definition
  asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid array definition: %s\n", driverName, functionName, arrayDesc.c_str());
  return "Invalid";
}

int omronUtilities::getBiggestDtype(structDtypeMap const& expandedMap, std::string structName)
{
  static const char *functionName = "getBiggestDtype";
  int thisSize = 0;
  int biggestSize = 0;
  std::string arrayDtype;
  std::vector<std::string> expandedRow = expandedMap.at(structName);
  const std::string arrayIdentifier = "\"ARRAY[";
  for (std::string dtype : expandedRow) {
    // Extract the dtype from an array definition string
    if (dtype.substr(0,7) == arrayIdentifier) {
      dtype = findArrayDtype(expandedMap, dtype);
      if (dtype == "Invalid")
      {
        return -1;
      }
    }
    if (dtype == "LREAL" || dtype == "ULINT" || dtype == "LINT" || dtype == "TIME") {return 8;}
    else if (dtype == "DWORD" || dtype == "UDINT" || dtype == "DINT" || dtype == "REAL") {thisSize = 4;}
    else if (dtype == "BOOL" || dtype == "WORD" || dtype == "UINT" || dtype == "INT") {thisSize = 2;}
    else if (dtype.substr(0,6) == "STRING" || dtype =="SINT" || dtype =="USINT") {thisSize = 1;}
    else if (dtype.substr(0,9)=="end:array" || dtype.substr(0,11)=="start:array") {continue;} 
    else {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Could not find the size of dtype: %s\n", driverName, functionName, dtype.c_str());
      return -1;
    }
    if (thisSize>biggestSize)
    {
      biggestSize=thisSize;
    }
  }
  return biggestSize;
}

// Return first raw dtype encountered, update alignment if we encounter the start or end of a struct
int omronUtilities::getEmbeddedAlignment(structDtypeMap const& expandedMap, std::string structName, std::string nextItem, size_t i){
  static const char *functionName = "getEmbeddedAlignment";
  std::vector<std::string> expandedRow = expandedMap.at(structName);
  std::string nextNextItem;
  int alignment = 0;
  std::string nextStruct = nextItem.substr(nextItem.find(':')+1,nextItem.size()-(nextItem.find(':')+1)); // removes start: and end: from the string
  // get the bit after : If this string is a valid struct name, lookup the alignment from that struct. If it is an array, then the alignment is calculated
  // based off the next dtype which may be raw or a struct. If a struct then get biggest from struct
  // Get next raw dtype

  if (nextStruct == "array"){
    // If the next item is an array start tag, then we need to get the alignment of the first item in the array, this may be a struct
    if (i+2 < expandedRow.size()){
      nextNextItem = expandedRow[i+2];
    }
    else {nextNextItem="none";}
    
    if (nextNextItem == "none") {alignment=0;}
    else if (nextNextItem == "LREAL" || nextNextItem == "ULINT" || nextNextItem == "LINT" || nextNextItem == "TIME") {alignment = 8;}
    else if (nextNextItem == "DWORD" || nextNextItem == "UDINT" || nextNextItem == "DINT" || nextNextItem == "REAL") {alignment = 4;}
    else if (nextNextItem == "BOOL" || nextNextItem == "WORD" || nextNextItem == "UINT" || nextNextItem == "INT") {alignment = 2;}
    else if (nextNextItem == "SINT" || nextNextItem == "USINT") {alignment = 1;}
    else if (nextNextItem.substr(0,6) == "STRING") {alignment = 1;}
    else if (nextNextItem == "start:array") {
      alignment = getEmbeddedAlignment(expandedMap, structName, expandedRow[i+3], i+1);
    }
    else if (expandedMap.find(nextNextItem)!=expandedMap.end()){
      // Check to see if the array dtype is a start:structName. If it is then we must lookup the biggest dtype in this struct
      alignment = getBiggestDtype(expandedMap, nextNextItem);
    }
    else {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Could not find the alignment rule for: %s\n", driverName, functionName, nextNextItem.c_str());
      return -1;
    }
  }
  else {
    // If the nextItem is a struct start or end, then we look up the largest item in the struct and use this to calculate alignment/padding
    try
    {
      expandedMap.at(nextStruct);
      alignment = getBiggestDtype(expandedMap, nextStruct);
    }
    catch (...)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid datatype: %s. Definition for: %s is invalid.\n", driverName, functionName, nextStruct.c_str(), structName.c_str());
      return -1;
    }
  }
  return alignment;
}

int omronUtilities::findOffsets(structDtypeMap const& expandedMap, std::string structName, std::unordered_map<std::string, std::vector<int>>& structMap)
{
  // We must calculate the size of each datatype
  // With a special case for strings which are sized based on their length
  // With a special case for arrays where their size is based on the number of elements and their type and a special case for bool arrays
  // We must also calculate the size of padding
  // For simple situations where the next item is a dtype then we just pad to the alignment of that dtype
  // If this item is a struct definition, then this offset is skipped and the next offset is padded based off the size of the biggest item in struct
  // If the next item is "end" then this offset is skipped and the next offset is padded based off biggest item in the previous struct
  // Arrays are padded as normal based off the alignment rules for their dtype

  static const char *functionName = "findOffsets";
  std::vector<int> newRow; // Stores the offsets as they are calculated
  std::vector<std::string> expandedRow = expandedMap.at(structName); // The row of datatypes that we are calculating the offsets for
  std::string nextItem;
  std::string prevItem;
  int thisOffset = 0; // offset position of the next item
  int dtypeSize = 0; // size of the basic dtype
  int paddingSize = 0; // size of padding
  int alignment = 0; // required alignment for thisOffset, can be 1,2,4,8
  int thisAlignment = 0; // temporarily stores the alignment returned from other functions which search for alignment
  bool insideBoolArray = false;
  int arrayBools = 1;
  int boolArrayOffset=0;
  int i = 0;

  for (std::string dtype : expandedRow){
    // If this item is end:structName, start:structName, end:array or start:array, then no offset is required so we skip to the next item
    if (dtype == "start:array"){
      insideBoolArray=true;
      i++;
      continue;
    }
    else if (dtype == "end:array"){
      arrayBools=1;
      insideBoolArray=false;
      i++;
      continue;
    }
    else if (dtype.substr(0,4) == "end:" || dtype.substr(0,6) == "start:"){
      i++;
      insideBoolArray=false;
      continue;
    }

    // Get next raw dtype
    if ((unsigned long int)(i+1) < expandedRow.size()){
      nextItem = expandedRow[i+1];
    }
    else {nextItem="none";}

    // Calculate size of the padding
    thisOffset += dtypeSize;
    if (thisOffset != 0){
      if (alignment == 0){
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Something went wrong while calculating the alignment for dtype: %s In struct: %s\n", driverName, functionName, dtype.c_str(), structName.c_str());
        return -1;
      }
      if (thisOffset % alignment != 0){
        paddingSize = alignment-(thisOffset % alignment);
      }
      else {paddingSize=0;}
    }
    else {paddingSize=0;}
    thisOffset += paddingSize;
    if (dtype=="BOOL"){
      newRow.push_back(thisOffset*8+boolArrayOffset); // We use the bit offset not byte offset for bools
    }
    else
      newRow.push_back(thisOffset); // This is the important bit which actually adds the start offset of the dtype currently being processed
    alignment = 0; // Reset alignment ready for next item

    // Calculate size of this dtype
    if (dtype=="SINT" || dtype=="USINT" ||dtype =="UINT" || dtype =="INT" || dtype =="WORD" || dtype =="BOOL" || 
          dtype =="UDINT" || dtype =="DINT" || dtype =="REAL" || dtype =="DWORD" || dtype =="ULINT" || dtype =="LINT" || 
            dtype == "LREAL" || dtype =="LWORD" || dtype =="TIME")
    {
      if (dtype == "BOOL" && insideBoolArray) {
        // When a bool is inside an array instead of taking up 2 bytes, they actually take up 1 bit and are stored together inside bytes
        // If the number of bools inside the same array is divisible by 8, then we move to the next byte
        // We calculate the bit offset elsewhere
        size_t remainder = arrayBools % 8;
        if (remainder == 0) {
          dtypeSize = 1;
          boolArrayOffset=0;
        }
        else {
          dtypeSize = 0;
          boolArrayOffset++;
        }
        arrayBools++;
      }
      else if (dtype =="SINT" || dtype =="USINT"){
        dtypeSize=1;
      }
      else if (dtype =="UINT" || dtype =="INT" || dtype =="WORD" || dtype =="BOOL"){
        dtypeSize=2;
      }
      else if (dtype =="UDINT" || dtype =="DINT" || dtype =="REAL" || dtype =="DWORD"){
        dtypeSize=4;
      }
      else if (dtype =="ULINT" || dtype =="LINT" || dtype == "LREAL" || dtype =="LWORD" || dtype=="TIME"){
        dtypeSize=8;
      }
    }
    else if (dtype.substr(0,7) == "STRING[")
    {
      auto ss = dtype.substr(7,dtype.size()-7);
      size_t strLength = 0;
      bool intFound = false;
      for (size_t i = 0; i<ss.size(); i++)
      {
        if (ss.substr(i,1)== "]")
        {
          intFound = true;
          try
          {
            strLength = std::stoi(ss.substr(0,i));
            if (strLength < 0) throw 1;
          }
          catch (...)
          {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, STRING length must be an integer! %s is invalid. Definition for struct: %s is invalid.\n", driverName, functionName, dtype.c_str(), structName.c_str());
            strLength = 0;
            return -1;
          }
          break;
        }
      }
      if (!intFound)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, STRING definition: %s does not specify size, definition for struct: %s is invalid.\n", driverName, functionName, dtype.c_str(), structName.c_str());
        return -1;
      }
      dtypeSize = strLength;
    }
    else {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Failed to parse user input datatype: %s. Definition for struct: %s is invalid.\n", driverName, functionName, dtype.c_str(), structName.c_str());
      return -1;
    }

    // Calculate the alignment rule of the next dtype, from this we can calculate the amount of padding required
    // nextItem should only be one of the raw dtypes at this point
    if ((nextItem =="BOOL" && insideBoolArray) || nextItem =="SINT" || nextItem =="USINT"){
      if (alignment < 1){alignment=1;}
    }
    else if (nextItem =="UINT" || nextItem =="INT" || nextItem =="WORD" || nextItem =="BOOL"){
      if (alignment < 2){alignment=2;}
    }
    else if (nextItem =="UDINT" || nextItem =="DINT" || nextItem =="REAL" || nextItem =="DWORD"){
      if (alignment < 4){alignment=4;}
    }
    else if (nextItem =="ULINT" || nextItem =="LINT" || nextItem == "LREAL" || nextItem =="LWORD" || nextItem =="TIME"){
      alignment=8;
    }
    else if (nextItem.substr(0,7) == "STRING["){
      if (alignment < 1){alignment=1;}
    }
    else if (nextItem == "none"){} // Next item is the end of the struct, so we need no padding from this dtype. However we may add padding elsewhere if the previous item is a struct 
    else if (nextItem.substr(0,4) == "end:" || nextItem.substr(0,6) == "start:")
    {
      // If nextItem is an array start or end, then set nextItem to the item after, this must be done recursively as that item could also be a struct
      // We keep searching until we find a raw dtype or the end of the map. If we find the end, then return none and no additional alignment is given
      // based on the next dtype. While searching we also update the alignment if we come across a struct start or end with a larger internal
      // dtype than the current alignment.
      thisAlignment = getEmbeddedAlignment(expandedMap, structName, nextItem, i);
      if (alignment < thisAlignment){alignment=thisAlignment;} // Alignment should be 0 at this point, but we check just in case
    }
    else {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Failed to calculate the alignment of: %s. Definition for struct: %s is invalid.\n", driverName, functionName, nextItem.c_str(), structName.c_str());
      return -1;
    }
    i++;
  }
  structMap[structName] = newRow;
  return 1;
}

omronUtilities::~omronUtilities()
{
  std::cout<<"omronUtilities shutting down"<<std::endl;
}
