/* A simple Mad Libs game which loads a random story from a file. */
/* Later a configuration pannel may be added. */
/*
Copyright (C) 2013 Andrew Makousky

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#define _WINDOWS
#ifdef _WINDOWS
#define F_BIN "b"
#else
#define F_BIN ""
#endif

#ifdef _MSC_VER
#include <io.h>
#else
#include <dirent.h> /* POSIX compliant code */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "bool.h"
#include "xmalloc.h"
#define ea_malloc xmalloc
#define ea_realloc xrealloc
#define ea_free xfree
#include "exparray.h"

typedef char* char_ptr;
typedef struct CacheInfo_tag CacheInfo;
typedef struct MadLibsStory_tag MadLibsStory;

EA_TYPE(char);
EA_TYPE(char_ptr);
EA_TYPE(unsigned);
EA_TYPE(MadLibsStory);

struct CacheInfo_tag
{
	unsigned numStories;
	/* Extra cache info */
	char* storyTitle;
	unsigned storyAddr;
	unsigned storySize;
};

struct MadLibsStory_tag
{
	char_array titleForPrev; /* When allowed to pick a story */
	char_ptr_array storyFragments;
	char_ptr_array customWords;
	unsigned_array wordRefs;
};

bool FindFiles(char_ptr_array* files);
unsigned TransToUnix(char* buffer, unsigned size);
bool ReadWholeFile(const char* filename, char** buffer, unsigned* size);
void AskQuestion(char* qBegin, char** wordDesc);
void FormatCustomWord(char* wordDesc);

/* Read the cache file in the current working directory.  Returns
   `true' on success, `false' on failure.  `cacheInfo' should be a
   preallocated array which is where the story cache will be
   stored.  */
bool ReadCacheFile(CacheInfo* cacheInfo, unsigned numFiles)
{
	FILE* fp;
	unsigned i;
	fp = fopen("strche.dat", "r" F_BIN);
	if (fp == NULL)
		return false;
	for (i = 0; i < numFiles; i++)
	{
		fread(&cacheInfo[i].numStories,
			  sizeof(cacheInfo[i].numStories), 1, fp);
	}
	fclose(fp);
	return true;
}

/* Generate the story cache file for the given Mad Libs story files.
   The story cache file is stored in the current working directory.
   Returns `true' on success, `false' on failure.  */
bool GenerateCacheFile(char_ptr_array* files)
{
	CacheInfo* cacheInfo;
	FILE* fp;
	char* buffer;
	unsigned fileSize;
	unsigned i;
	cacheInfo = (CacheInfo*)xmalloc(sizeof(CacheInfo) * files->len);
	/* For each story file, count the number of END STORY tags.  */
	for (i = 0; i < files->len; i++)
	{
		unsigned j;
		cacheInfo[i].numStories = 0;
		ReadWholeFile(files->d[i], &buffer, &fileSize);
		for (j = 0; j < fileSize; j++)
		{
			/* Check for end tag.  */
			const char* endTag = "\n\nEND STORY";
			unsigned endTagLen = 11;
			if (j + endTagLen <= fileSize &&
				!strncmp(&buffer[j], endTag, endTagLen))
				cacheInfo[i].numStories++;
		}
		xfree(buffer);
	}
	/* Write the new cache file.  */
	fp = fopen("strche.dat", "w" F_BIN);
	if (fp == NULL)
	{
		xfree(cacheInfo);
		return false;
	}
	for (i = 0; i < files->len; i++)
	{
		fwrite(&cacheInfo[i].numStories,
			   sizeof(cacheInfo[i].numStories), 1, fp);
	}
	fclose(fp);
	xfree(cacheInfo);
	return true;
}

/* Parse a text buffer containing a story library and store it in the
   given Mad Libs story array.  `fileSize' should be the length of the
   text buffer, not including the null terminator.  */
void ParseStoryLibrary(char* buffer, unsigned fileSize,
					   MadLibsStory_array* stories)
{
	unsigned curWordRef = 0;
	bool inTitle = true; /* The first line should be a title.  */
	unsigned i;

	/* We have to format three arrays: story fragments, custom words,
	   and word references.  Story fragments and word references are
	   interleaved to create the story.  Story fragments are formed by
	   storing pointers into the file buffer and writing null
	   characters at the ends of fragments (usually at a '('
	   character).  */

	/* Allocate memory for the first story.  */
	EA_SET_SIZE(*stories, stories->len + 1);
	EA_INIT(char, EA_BACK(*stories).titleForPrev, 16);
	EA_APPEND(EA_BACK(*stories).titleForPrev, '\0');
	EA_INIT(char_ptr, EA_BACK(*stories).storyFragments, 16);
	EA_INIT(char_ptr, EA_BACK(*stories).customWords, 16);
	EA_INIT(unsigned, EA_BACK(*stories).wordRefs, 16);
	EA_APPEND(EA_BACK(*stories).storyFragments, &buffer[0]);
	for (i = 0; i < fileSize; i++)
	{
		if (buffer[i] != '(')
		{
			/* Keep parsing for a story fragment until either the end
			   of the story is found or a custom word is found.  */
			char* curFragment = &buffer[i];
			for (; i < fileSize && buffer[i] != '('; i++)
			{
				/* Check for end tag.  */
				const char* endTag = "\n\nEND STORY";
				unsigned endTagLen = 11;
				if (i + endTagLen <= fileSize &&
					!strncmp(&buffer[i], endTag, endTagLen))
				{
					/* Terminate the last story fragment.  */
					buffer[i] = '\0';
					/* We found the END STORY tag.  */
					i += endTagLen;
					if (i == fileSize)
						goto storyEnd;
					else if (buffer[i] != '\n')
						break;
					/* if (buffer[i] != '\n') */
					i++; /* Skip double newline.  */
					/* Allocate memory for the next story.  */
					EA_SET_SIZE(*stories, stories->len + 1);
					EA_INIT(char, EA_BACK(*stories).titleForPrev, 16);
					EA_APPEND(EA_BACK(*stories).titleForPrev, '\0');
					EA_INIT(char_ptr, EA_BACK(*stories).storyFragments, 16);
					EA_INIT(char_ptr, EA_BACK(*stories).customWords, 16);
					EA_INIT(unsigned, EA_BACK(*stories).wordRefs, 16);
					EA_APPEND(EA_BACK(*stories).storyFragments,
							  &buffer[i+1]);
					/* Reset proper variables.  */
					curWordRef = 0;
					inTitle = true;
					goto storyEnd;
				}

				if (inTitle == true)
				{
					if (buffer[i] != '\n')
					{
						EA_INSERT(EA_BACK(*stories).titleForPrev,
						  EA_BACK(*stories).titleForPrev.len - 1, buffer[i]);
					}
					else
					{
						inTitle = false;
						/* if (buffer[i+1] != '\n') */
						/* Then we should tell the user about this */
						/* We might temporarily fix it too */
					}
				}
			}
			i--; /* Don't skip the '(' on the next loop.  */
			storyEnd: ;
		}
		else
		{
			/* Process a potential custom word.  */
			i++; /* Skip '(' */
			if (buffer[i] == '(')
			{
				/* These parentheses are meant for the story content */
				while (i < fileSize && buffer[i] == '(')
					i++;
				/* Don't skip the next character on the next loop.  */
				if (i < fileSize)
					i--;
			}
			else
			{
				/* We found a custom word.  */
				char* customWord;
				unsigned customWordLen = 0;
				bool foundReuse = false;
				unsigned j;
				/* Terminate the last story fragment.  */
				buffer[i-1] = '\0';
				/* Save the custom word.  */
				customWord = &buffer[i];
				for (; i < fileSize && buffer[i] != ')'; i++)
					customWordLen++;
				/* Search for a match with this custom word and
				   previous custom words.  */
				for (j = 0; j < EA_BACK(*stories).customWords.len; j++)
				{
					if (!strncmp(EA_BACK(*stories).customWords.d[j],
								 customWord, customWordLen))
					{
						/* We have a match.  Fill in the proper word
						   reference.  */
						EA_APPEND(EA_BACK(*stories).wordRefs, j);
						foundReuse = true;
						break;
					}
				}
				if (foundReuse == false)
				{
					/* No match found.  Store the new custom word.  */
					buffer[i] = '\0';
					EA_APPEND(EA_BACK(*stories).customWords,
							  customWord);
					EA_APPEND(EA_BACK(*stories).wordRefs,
							  curWordRef);
					curWordRef++;
				}
				EA_APPEND(EA_BACK(*stories).storyFragments,
						  &buffer[i+1]);
			}
			if (inTitle == true)
			{
				EA_INSERT(EA_BACK(*stories).titleForPrev,
						  EA_BACK(*stories).titleForPrev.len - 1, '?');
			}
		}
	}
	if (inTitle == true)
	{
		/* We have a newline at the end of the file, but of course no
		   story.  */
		EA_POP_BACK(*stories);
	}
}

/* Prompt the user for each custom word and store it in the Mad Libs
   Story info.  The custom words are dynamically allocated strings
   that must be freed when they are no longer needed.  */
void AskWords(MadLibsStory* story)
{
	bool sayAnotherNext = false;
	unsigned i;
	FormatCustomWord(story->customWords.d[0]);
	for (i = 0; i < story->customWords.len; i++)
	{
		/* This flag is set at the end of the loop.  */
		if (sayAnotherNext == true)
		{
			sayAnotherNext = false;
			/* If the next type of word is the same type, then set the
			   flag so next time we say "Type another ..." */
			if (i < story->customWords.len - 1)
				FormatCustomWord(story->customWords.d[i+1]);
			if (i < story->customWords.len - 1 &&
				!strcmp(story->customWords.d[i],
						story->customWords.d[i+1]))
				sayAnotherNext = true;
			/* Now we can change the word when we ask the question.  */
			AskQuestion("Type another ", &story->customWords.d[i]);
		}
		else
		{
			char* accptdVowls = "aeiou";
			bool useAn = false;
			unsigned j;
			/* If the next type of word is the same type, then set the
			   flag so next time we say "Type another ..." */
			if (i < story->customWords.len - 1)
				FormatCustomWord(story->customWords.d[i+1]);
			if (i < story->customWords.len - 1 &&
				!strcmp(story->customWords.d[i],
						story->customWords.d[i+1]))
				sayAnotherNext = true;
			/* If the first letter of the word is a vowel (a, e, i, o,
			   u, not including y), then say "Type an ..." */
			for (j = 0; j < 5; j++)
			{
				if (story->customWords.d[i][0] == accptdVowls[j])
				{
					useAn = true;
					break;
				}
			}
			if (useAn == true)
				AskQuestion("Type an ", &story->customWords.d[i]);
			/* Otherwise, just say "Type a ..." */
			else
				AskQuestion("Type a ", &story->customWords.d[i]);
		}
	}
}

int main(int argc, char* argv[])
{
	int retval = 0;
	char_ptr_array files;
	CacheInfo* cacheInfo = NULL;
	unsigned fileID; /* Which story file?  */

	char* buffer = NULL;
	unsigned fileSize;
	MadLibsStory_array stories;
	unsigned story;

	unsigned i;

	/* First find all the story files in the directory.  */
	EA_INIT(char_ptr, files, 16);
	EA_INIT(MadLibsStory, stories, 16);
	if (!FindFiles(&files))
	{
		fputs("ERROR: Could not find any story files.\n", stderr);
		retval = 1; goto cleanup;
	}

	cacheInfo = (CacheInfo*)xmalloc(sizeof(CacheInfo) * files.len);
	if (!ReadCacheFile(cacheInfo, files.len))
	{
		fputs("ERROR: Could not open cache file.\n", stderr);
		fputs(
"A new cache file will be generated.  Restart Mad Libs to use the new\n\
cache file.\n", stderr);
		xfree(cacheInfo); cacheInfo = NULL;
		GenerateCacheFile(&files);
		retval = 1; goto cleanup;
	}

	/* Prepare psudorandom number table.  */
	srand(time(NULL));

	{ /* Compute that: */
		unsigned totNumStories = 0;
		for (i = 0; i < files.len; i++)
			totNumStories += cacheInfo[i].numStories;
		if (totNumStories == 0)
			totNumStories = 1;

		/* Pick a story.  */
		if (argc > 1)
			sscanf(argv[1], "%d", &story);
		else
			story = rand() % totNumStories;
	}

	{ /* Find which file should be opened.  */
		unsigned prevStories = 0;
		for (i = 0; i < files.len; i++)
		{
			if (story < cacheInfo[i].numStories + prevStories)
			{
				story -= prevStories;
				fileID = i;
				break;
			}
			prevStories += cacheInfo[i].numStories;
		}
	}

	/* We're done with cacheInfo.  */
	xfree(cacheInfo); cacheInfo = NULL;

	if (!ReadWholeFile(files.d[fileID], &buffer, &fileSize))
	{
		fprintf(stderr, "ERROR: Could not open story library: %s\n",
				files.d[fileID]);
		retval = 1; goto cleanup;
	}
	ParseStoryLibrary(buffer, fileSize, &stories);
	AskWords(&stories.d[story]);

	/* Display the story.  */
	{
		fputs("\n", stdout);
		for (i = 0; i < stories.d[story].storyFragments.len - 1; i++)
		{
			fputs(stories.d[story].storyFragments.d[i], stdout);
			fputs(stories.d[story].
				  customWords.d[stories.d[story].wordRefs.d[i]], stdout);
		}
		fputs(stories.d[story].storyFragments.d[i], stdout);
		fputs("\n", stdout);
	}

	retval = 0;

cleanup:
	for (i = 0; i < files.len; i++)
		xfree(files.d[i]);
	EA_DESTROY(files);
	for (i = 0; i < stories.len; i++)
	{
		unsigned j;
		EA_DESTROY(stories.d[i].titleForPrev);
		EA_DESTROY(stories.d[i].storyFragments);
		if (i == story)
		{
			for (j = 0; j < stories.d[i].customWords.len; j++)
				xfree(stories.d[i].customWords.d[j]);
		}
		EA_DESTROY(stories.d[i].customWords);
		EA_DESTROY(stories.d[i].wordRefs);
	}
	EA_DESTROY(stories);
	xfree(buffer);
	return retval;
}

int qsort_stringlist(const void* e1, const void* e2)
{
	return strcmp(*(char**)e1, *(char**)e2);
}

bool FindFiles(char_ptr_array* files)
{
#ifdef _MSC_VER

	struct _finddata_t findData;
	intptr_t findHandle;

	findHandle = _findfirst("*.mlb", &findData);
	if (findHandle == -1)
		return false;

	files->d[files->len] = (char_ptr)xmalloc(strlen(findData.name) + 1);
	strcpy(files->d[files->len], findData.name);
	EA_ADD(*files);
	while (_findnext(findHandle, &findData) == 0)
	{
		files->d[files->len] = (char_ptr)xmalloc(strlen(findData.name) + 1);
		strcpy(files->d[files->len], findData.name);
		EA_ADD(*files);
	}
	_findclose(findHandle);
	qsort(files->d, files->len, sizeof(char*), qsort_stringlist);
	return true;

#else

	DIR *d = opendir(".");
	if (d == NULL)
		return false;

	struct dirent *de;
	while (de = readdir(d))
	{
		files->d[files->len] = (char_ptr)xmalloc(strlen(de->d_name) + 1);
		strcpy(files->d[files->len], de->d_name);
		EA_ADD(*files);
		if (strstr(files->d[files->len-1], ".mlb") !=
			files->d[files->len-1] + strlen(files->d[files->len-1]) - 4)
			files->len--;
	}
	closedir(d);
	if (files->len > 0)
	{
		qsort(files->d, files->len, sizeof(char*), qsort_stringlist);
		return true;
	}
	else
		return false;

#endif
}

void AskQuestion(char* qBegin, char** wordDesc)
{
	char* answer = NULL;
	unsigned answerLen = 0;
	printf("%s%s: ", qBegin, *wordDesc);
	fflush(stdout);
	do
	{
		char* newAnsPart;
		answer = (char*)xrealloc(answer, answerLen + 129);
		newAnsPart = &answer[answerLen];
		fgets(newAnsPart, 129, stdin);
		answerLen += strlen(newAnsPart);
	} while (answer[answerLen-1] != '\n');
	answer = (char*)xrealloc(answer, answerLen + 1);
	answer[answerLen-1] = '\0'; /* Delete the newline.  */
	/* Do not xfree() wordDesc because it is part of the entire file's
	   allocation.  */
	/* xfree(*wordDesc); */
	(*wordDesc) = answer;
}

/* Erases the numeric ID from the custom word description */
void FormatCustomWord(char* wordDesc)
{
	/* NOTE: Here we consider it pointless to have only a number for
	   the word description.  */
	unsigned numBegin;
	bool inNumber = true;
	for (numBegin = strlen(wordDesc) - 1;
		 numBegin > 0 && inNumber == true; numBegin--)
	{
		if (!isdigit((int)wordDesc[numBegin]))
			inNumber = false;
	}
	numBegin += 2;	/* Undo the extra numBegin-- and...
					   Don't erase the last letter of the description.  */
	wordDesc[numBegin] = '\0';
}

/* Translates a text buffer to Unix line endings in place.  Returns
   the new size of the data, which may have shrunken.  `size' is the
   length of the string, not including the null character.  The null
   terminator is appended automatically.  */
unsigned TransToUnix(char* buffer, unsigned size)
{
	unsigned i, j;
	for (i = 0, j = 0; i < size; i++, j++)
	{
		if (buffer[i] != '\r')
			buffer[j] = buffer[i];
		else
		{
			if (i + 1 < size && buffer[i+1] == '\n') /* CR+LF */
				i++;
			buffer[j] = '\n';
		}
	}
	buffer[j] = '\0';
	return j;
}

bool ReadWholeFile(const char* filename, char** buffer, unsigned* fileSize)
{
	FILE* fp;
	char* locBuffer;
	unsigned locFileSize;
	fp = fopen(filename, "r" F_BIN);
	if (fp == NULL)
	{
		(*buffer) = (char*)xmalloc(1);
		(*buffer)[0] = '\0';
		(*fileSize) = 0;
		return false;
	}
	fseek(fp, 0, SEEK_END);
	locFileSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	locBuffer = (char*)xmalloc(locFileSize + 1 + 1);
	fread(locBuffer, locFileSize, 1, fp);
	locBuffer[locFileSize] = '\0';
	locBuffer[locFileSize+1] = '\0'; /* Leave a safety margin.  */
	fclose(fp);
	/* Since we read the whole file this way, we will have to
	   properly format newlines. */
	locFileSize = TransToUnix(locBuffer, locFileSize);
	locBuffer[locFileSize+1] = '\0';
	(*buffer) = locBuffer;
	(*fileSize) = locFileSize;
	return true;
}
