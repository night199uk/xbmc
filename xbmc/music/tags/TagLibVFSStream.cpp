/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */
#include "limits.h"
#include "TagLibVFSStream.h"
#include "filesystem/File.h"
#include "utils/StdString.h"
#include "utils/log.h"
#include <taglib/tiostream.h>

using namespace XFILE;
using namespace TagLib;
using namespace MUSIC_INFO;
using namespace std;

#ifdef TARGET_WINDOWS
#pragma comment(lib, "tag.lib")
#endif

/*!
 * Construct a File object and opens the \a file.  \a file should be a
 * be an XBMC Vfile.
 */
TagLibVFSStream::TagLibVFSStream(const string& strFileName, bool readOnly)
{
  m_bIsOpen = true;
  if (readOnly)
  {
    if (!m_file.Open(strFileName))
      m_bIsOpen = false;
  }
  else
  {
    if (!m_file.OpenForWrite(strFileName))
      m_bIsOpen = false;
  }
  m_strFileName = strFileName;
}

/*!
 * Destroys this ByteVectorStream instance.
 */
TagLibVFSStream::~TagLibVFSStream()
{
  m_file.Close();
}

/*!
 * Returns the file name in the local file system encoding.
 */
FileName TagLibVFSStream::name() const
{
  return m_strFileName.c_str();
}

/*!
 * Reads a block of size \a length at the current get pointer.
 */
ByteVector TagLibVFSStream::readBlock(ulong length)
{
  ByteVector byteVector(static_cast<uint>(length));
  byteVector.resize(m_file.Read(byteVector.data(), length));
  return byteVector;
}

/*!
 * Attempts to write the block \a data at the current get pointer.  If the
 * file is currently only opened read only -- i.e. readOnly() returns true --
 * this attempts to reopen the file in read/write mode.
 *
 * \note This should be used instead of using the streaming output operator
 * for a ByteVector.  And even this function is significantly slower than
 * doing output with a char[].
 */
void TagLibVFSStream::writeBlock(const ByteVector &data)
{
  m_file.Write(data.data(), data.size());
}

/*!
 * Insert \a data at position \a start in the file overwriting \a replace
 * bytes of the original content.
 *
 * \note This method is slow since it requires rewriting all of the file
 * after the insertion point.
 */
void TagLibVFSStream::insert(const ByteVector &data, ulong start, ulong replace)
{
  if (data.size() == replace)
  {
    seek(start);
    writeBlock(data);
    return;
  }
  else if (data.size() < replace)
  {
    seek(start);
    writeBlock(data);
    removeBlock(start + data.size(), replace - data.size());
  }

  // Woohoo!  Faster (about 20%) than id3lib at last.  I had to get hardcore
  // and avoid TagLib's high level API for rendering just copying parts of
  // the file that don't contain tag data.
  //
  // Now I'll explain the steps in this ugliness:

  // First, make sure that we're working with a buffer that is longer than
  // the *differnce* in the tag sizes.  We want to avoid overwriting parts
  // that aren't yet in memory, so this is necessary.
  ulong bufferLength = bufferSize();

  while (data.size() - replace > bufferLength)
    bufferLength += bufferSize();

  // Set where to start the reading and writing.
  long readPosition = start + replace;
  long writePosition = start;
  ByteVector buffer;
  ByteVector aboutToOverwrite(static_cast<uint>(bufferLength));

  // This is basically a special case of the loop below.  Here we're just
  // doing the same steps as below, but since we aren't using the same buffer
  // size -- instead we're using the tag size -- this has to be handled as a
  // special case.  We're also using File::writeBlock() just for the tag.
  // That's a bit slower than using char *'s so, we're only doing it here.
  seek(readPosition);
  int bytesRead = m_file.Read(aboutToOverwrite.data(), bufferLength);
  readPosition += bufferLength;

  seek(writePosition);
  writeBlock(data);
  writePosition += data.size();

  buffer = aboutToOverwrite;
  buffer.resize(bytesRead);

  // Ok, here's the main loop.  We want to loop until the read fails, which
  // means that we hit the end of the file.
  while (!buffer.isEmpty())
  {
    // Seek to the current read position and read the data that we're about
    // to overwrite.  Appropriately increment the readPosition.
    seek(readPosition);
    bytesRead = m_file.Read(aboutToOverwrite.data(), bufferLength);
    aboutToOverwrite.resize(bytesRead);
    readPosition += bufferLength;

    // Check to see if we just read the last block.  We need to call clear()
    // if we did so that the last write succeeds.
    if (ulong(bytesRead) < bufferLength)
      clear();

    // Seek to the write position and write our buffer.  Increment the
    // writePosition.
    seek(writePosition);
    m_file.Write(buffer.data(), buffer.size());
    writePosition += buffer.size();

    buffer = aboutToOverwrite;
    bufferLength = bytesRead;
  }
}

/*!
 * Removes a block of the file starting a \a start and continuing for
 * \a length bytes.
 *
 * \note This method is slow since it involves rewriting all of the file
 * after the removed portion.
 */
void TagLibVFSStream::removeBlock(ulong start, ulong length)
{
  ulong bufferLength = bufferSize();

  long readPosition = start + length;
  long writePosition = start;

  ByteVector buffer(static_cast<uint>(bufferLength));

  ulong bytesRead = 1;

  while(bytesRead != 0)
  {
    seek(readPosition);
    bytesRead = m_file.Read(buffer.data(), bufferLength);
    readPosition += bytesRead;

    // Check to see if we just read the last block.  We need to call clear()
    // if we did so that the last write succeeds.
    if(bytesRead < bufferLength)
      clear();

    seek(writePosition);
    m_file.Write(buffer.data(), bytesRead);
    writePosition += bytesRead;
  }
  truncate(writePosition);
}

/*!
 * Returns true if the file is read only (or if the file can not be opened).
 */
bool TagLibVFSStream::readOnly() const
{
  return m_bIsReadOnly;
}

/*!
 * Since the file can currently only be opened as an argument to the
 * constructor (sort-of by design), this returns if that open succeeded.
 */
bool TagLibVFSStream::isOpen() const
{
  return m_bIsOpen;
}

/*!
 * Move the I/O pointer to \a offset in the file from position \a p.  This
 * defaults to seeking from the beginning of the file.
 *
 * \see Position
 */
void TagLibVFSStream::seek(long offset, Position p)
{
  switch(p)
  {
    case Beginning:
      m_file.Seek(offset, SEEK_SET);
      break;
    case Current:
      m_file.Seek(offset, SEEK_CUR);
      break;
    case End:
      m_file.Seek(offset, SEEK_END);
      break;
  }
}

/*!
 * Reset the end-of-file and error flags on the file.
 */
void TagLibVFSStream::clear()
{
}

/*!
 * Returns the current offset within the file.
 */
long TagLibVFSStream::tell() const
{
  int64_t pos = m_file.GetPosition();
  if(pos > LONG_MAX)
    return -1;
  else
    return (long)pos;
}

/*!
 * Returns the length of the file.
 */
long TagLibVFSStream::length()
{
  return m_file.GetLength();
}

/*!
 * Truncates the file to a \a length.
 */
void TagLibVFSStream::truncate(long length)
{
  m_file.Truncate(length);
}
