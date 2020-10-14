/*
 *   Copyright (C) 2009-2018,2020 by Jonathan Naylor G4KLX
 *   Copyright (C) 2017 by Andy Uribe CA6JAU
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "Config.h"
#include "Globals.h"
#include "M17TX.h"

#include "M17Defines.h"

// Generated using rcosdesign(0.2, 8, 5, 'sqrt') in MATLAB
static q15_t RRC_0_2_FILTER[] = {0, 0, 0, 0, 850, 219, -720, -1548, -1795, -1172, 237, 1927, 3120, 3073, 1447, -1431, -4544, -6442,
                                 -5735, -1633, 5651, 14822, 23810, 30367, 32767, 30367, 23810, 14822, 5651, -1633, -5735, -6442,
                                 -4544, -1431, 1447, 3073, 3120, 1927, 237, -1172, -1795, -1548, -720, 219, 850}; // numTaps = 45, L = 5
const uint16_t RRC_0_2_FILTER_PHASE_LEN = 9U; // phaseLength = numTaps/L

const q15_t M17_LEVELA =  948;
const q15_t M17_LEVELB =  316;
const q15_t M17_LEVELC = -316;
const q15_t M17_LEVELD = -948;

const uint8_t M17_START_SYNC = 0x77U;
const uint8_t M17_END_SYNC   = 0xFFU;
const uint8_t M17_HANG       = 0x00U;

CM17TX::CM17TX() :
m_buffer(TX_BUFFER_LEN),
m_modFilter(),
m_modState(),
m_poBuffer(),
m_poLen(0U),
m_poPtr(0U),
m_txDelay(240U),      // 200ms
m_txHang(4800U),      // 4s
m_txCount(0U)
{
  ::memset(m_modState, 0x00U, 16U * sizeof(q15_t));

  m_modFilter.L           = M17_RADIO_SYMBOL_LENGTH;
  m_modFilter.phaseLength = RRC_0_2_FILTER_PHASE_LEN;
  m_modFilter.pCoeffs     = RRC_0_2_FILTER;
  m_modFilter.pState      = m_modState;
}


void CM17TX::process()
{
  // If we have M17 data to transmit, do so.
  if (m_poLen == 0U && m_buffer.getData() > 0U) {
    if (!m_tx) {
      for (uint16_t i = 0U; i < m_txDelay; i++)
        m_poBuffer[m_poLen++] = M17_START_SYNC;
    } else {
      for (uint8_t i = 0U; i < M17_FRAME_LENGTH_BYTES; i++) {
        uint8_t c = m_buffer.get();
        m_poBuffer[m_poLen++] = c;
      }
    }

    m_poPtr = 0U;
  }

  if (m_poLen > 0U) {
    // Transmit M17 data.
    uint16_t space = io.getSpace();

    while (space > (4U * M17_RADIO_SYMBOL_LENGTH)) {
      uint8_t c = m_poBuffer[m_poPtr++];
      writeByte(c);

      // Reduce space and reset the hang timer.
      space -= 4U * M17_RADIO_SYMBOL_LENGTH;
      if (m_duplex)
        m_txCount = m_txHang;

      if (m_poPtr >= m_poLen) {
        m_poPtr = 0U;
        m_poLen = 0U;
        return;
      }
    }
  } else if (m_txCount > 0U) {
    // Transmit silence until the hang timer has expired.
    uint16_t space = io.getSpace();

    while (space > (4U * M17_RADIO_SYMBOL_LENGTH)) {
      writeSilence();

      space -= 4U * M17_RADIO_SYMBOL_LENGTH;
      m_txCount--;

      if (m_txCount == 0U)
        return;
    }
  }
}

uint8_t CM17TX::writeData(const uint8_t* data, uint8_t length)
{
  if (length != (M17_FRAME_LENGTH_BYTES + 1U))
    return 4U;

  uint16_t space = m_buffer.getSpace();
  if (space < M17_FRAME_LENGTH_BYTES)
    return 5U;

  for (uint8_t i = 0U; i < M17_FRAME_LENGTH_BYTES; i++)
    m_buffer.put(data[i + 1U]);

  return 0U;
}

void CM17TX::writeByte(uint8_t c)
{
  q15_t inBuffer[4U];
  q15_t outBuffer[M17_RADIO_SYMBOL_LENGTH * 4U];

  const uint8_t MASK = 0xC0U;

  for (uint8_t i = 0U; i < 4U; i++, c <<= 2) {
    switch (c & MASK) {
      case 0xC0U:
        inBuffer[i] = M17_LEVELA;
        break;
      case 0x80U:
        inBuffer[i] = M17_LEVELB;
        break;
      case 0x00U:
        inBuffer[i] = M17_LEVELC;
        break;
      default:
        inBuffer[i] = M17_LEVELD;
        break;
    }
  }

  ::arm_fir_interpolate_q15(&m_modFilter, inBuffer, outBuffer, 4U);

  io.write(STATE_M17, outBuffer, M17_RADIO_SYMBOL_LENGTH * 4U);
}

void CM17TX::writeSilence()
{
  q15_t inBuffer[4U] = {0x00U, 0x00U, 0x00U, 0x00U};
  q15_t outBuffer[M17_RADIO_SYMBOL_LENGTH * 4U];

  ::arm_fir_interpolate_q15(&m_modFilter, inBuffer, outBuffer, 4U);

  io.write(STATE_M17, outBuffer, M17_RADIO_SYMBOL_LENGTH * 4U);
}

void CM17TX::setTXDelay(uint8_t delay)
{
  m_txDelay = 600U + uint16_t(delay) * 12U;        // 500ms + tx delay

  if (m_txDelay > 1200U)
    m_txDelay = 1200U;
}

uint8_t CM17TX::getSpace() const
{
  return m_buffer.getSpace() / M17_FRAME_LENGTH_BYTES;
}

void CM17TX::setParams(uint8_t txHang)
{
  m_txHang = txHang * 1200U;
}