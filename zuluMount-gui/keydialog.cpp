/*
 *
 *  Copyright (c) 2012
 *  name : Francis Banyikwa
 *  email: mhogomchungu@gmail.com
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "deviceoffset.h"

#include "keydialog.h"
#include "ui_keydialog.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QDir>
#include <QTableWidget>
#include <QDebug>
#include <QFile>

#include "bin_path.h"
#include "../zuluCrypt-gui/dialogmsg.h"
#include "../zuluCrypt-gui/task.h"
#include "../zuluCrypt-cli/constants.h"
#include "plugin_path.h"
#include "../zuluCrypt-gui/utility.h"
#include "../zuluCrypt-gui/lxqt_wallet/frontend/lxqt_wallet.h"
#include "mountoptions.h"
#include "../zuluCrypt-gui/tcrypt.h"
#include "zulumounttask.h"
#include "../zuluCrypt-gui/task.h"
#include "zulumounttask.h"
#include "veracrypt_support.h"
#include "truecrypt_support.h"

#define KWALLET         "kde wallet"
#define INTERNAL_WALLET "internal wallet"
#define GNOME_WALLET    "gnome wallet"

/*
 * this ugly global variable is defined in zulucrypt.cpp to prevent multiple prompts when opening multiple volumes
 */
static QString _internalPassWord ;

keyDialog::keyDialog( QWidget * parent,QTableWidget * table,const volumeEntryProperties& e ) :
	QDialog( parent ),m_ui( new Ui::keyDialog )
{
	m_ui->setupUi( this ) ;
	m_ui->checkBoxShareMountPoint->setToolTip( utility::shareMountPointToolTip() ) ;
	m_table = table ;
	m_path = e.volumeName() ;
	m_working = false ;
	m_volumeIsEncFs = e.fileSystem() == "encfs" ;

	QString msg ;

	if( e.fileSystem() == "crypto_LUKS" ){

		msg = tr( "Mount A LUKS volume in \"%1\"").arg( m_path ) ;
	}else{
		msg = tr( "Mount An Encrypted Volume In \"%1\"").arg( m_path ) ;
	}

	this->setWindowTitle( msg ) ;

	m_ui->lineEditMountPoint->setText( m_path ) ;
	m_ui->pbOpenMountPoint->setIcon( QIcon( ":/folder.png" ) ) ;

	m_menu = new QMenu( this ) ;

	connect( m_menu,SIGNAL( triggered( QAction * ) ),this,SLOT( pbPluginEntryClicked( QAction * ) ) ) ;

	this->setFixedSize( this->size() ) ;
	this->setWindowFlags( Qt::Window | Qt::Dialog ) ;
	this->setFont( parent->font() ) ;

	m_ui->lineEditKey->setFocus() ;

	m_ui->checkBoxOpenReadOnly->setChecked( utility::getOpenVolumeReadOnlyOption( "zuluMount-gui" ) ) ;

	m_ui->pbkeyOption->setEnabled( false ) ;

	m_ui->lineEditKey->setEchoMode( QLineEdit::Password ) ;

	connect( m_ui->pbOptions,SIGNAL( clicked() ),this,SLOT( pbOptions() ) ) ;
	connect( m_ui->pbCancel,SIGNAL( clicked() ),this,SLOT( pbCancel() ) ) ;
	connect( m_ui->pbOpen,SIGNAL( clicked() ),this,SLOT( pbOpen() ) ) ;
	connect( m_ui->pbkeyOption,SIGNAL( clicked() ),this,SLOT( pbkeyOption() ) ) ;
	connect( m_ui->pbOpenMountPoint,SIGNAL( clicked() ),this,SLOT( pbMountPointPath() ) ) ;
	connect( m_ui->checkBoxOpenReadOnly,SIGNAL( stateChanged( int ) ),this,SLOT( cbMountReadOnlyStateChanged( int ) ) ) ;
	connect( m_ui->cbKeyType,SIGNAL( currentIndexChanged( int ) ),this,SLOT( cbActicated( int ) ) ) ;

	m_ui->pbOpenMountPoint->setVisible( false ) ;

	m_point = utility::mountPathPostFix( m_path.split( "/" ).last() ) ;

	m_ui->lineEditMountPoint->setText( m_point ) ;

	QAction * ac = new QAction( this ) ;
	QKeySequence s( Qt::CTRL + Qt::Key_F ) ;
	ac->setShortcut( s ) ;
	connect( ac,SIGNAL( triggered() ),this,SLOT( showOffSetWindowOption() ) ) ;
	this->addAction( ac ) ;

	m_menu_1 = new QMenu( this ) ;

	auto _add_action = [ & ]( const QString& e ){

		ac = m_menu_1->addAction( e ) ;
		ac ->setEnabled( !m_volumeIsEncFs ) ;
	} ;

	_add_action( tr( "Set File System Options" ) ) ;
	_add_action( tr( "Set Volume Offset" ) ) ;
	_add_action( tr( "Set Volume As VeraCrypt Volume" ) ) ;

	m_ui->cbKeyType->addItem( tr( "Key" ) ) ;
	m_ui->cbKeyType->addItem( tr( "Keyfile" ) ) ;
	m_ui->cbKeyType->addItem( tr( "Plugin" ) ) ;

	if( m_volumeIsEncFs ){

		m_ui->checkBoxShareMountPoint->setEnabled( false ) ;
	}else{
		#if VERACRYPT_SUPPORT
			m_ui->cbKeyType->addItem( tr( "TrueCrypt/VeraCrypt Keys" ) ) ;
		#else
			m_ui->cbKeyType->addItem( tr( "TrueCrypt keys" ) ) ;
		#endif
	}

	connect( m_menu_1,SIGNAL( triggered( QAction * ) ),this,SLOT( doAction( QAction * ) ) ) ;

	m_veraCryptWarning.setWarningLabel( m_ui->veraCryptWarning ) ;

	this->installEventFilter( this ) ;
}

bool keyDialog::eventFilter( QObject * watched,QEvent * event )
{
	if( utility::eventFilter( this,watched,event ) ){
		this->pbCancel() ;
		return true ;
	}else{
		return false ;
	}
}

void keyDialog::tcryptCancelled( void )
{
	m_key.clear() ;
	m_keyFiles.clear() ;
	m_ui->cbKeyType->setCurrentIndex( keyDialog::Key ) ;
	m_ui->lineEditKey->setText( QString() ) ;
	this->enableAll() ;
}

void keyDialog::tcryptGui()
{
	this->disableAll() ;
	m_ui->lineEditKey->setText( QString() ) ;

	tcrypt * t = new tcrypt( this ) ;
	connect( t,SIGNAL( Keys( QString,QStringList ) ),this,SLOT( keys( QString,QStringList ) ) ) ;
	connect( t,SIGNAL( cancelled() ),this,SLOT( tcryptCancelled() ) ) ;

	t->ShowUI() ;
}

void keyDialog::keys( QString key,QStringList keyFiles )
{
	m_key = key ;
	m_keyFiles = keyFiles ;
	this->openVolume() ;
	m_ui->cbKeyType->setCurrentIndex( keyDialog::Key ) ;
	m_ui->lineEditKey->setText( QString() ) ;
	m_ui->lineEditKey->setEnabled( false ) ;
}

void keyDialog::pbOptions()
{
	m_menu_1->exec( QCursor::pos() ) ;
}

void keyDialog::showOffSetWindowOption()
{
	deviceOffset * d = new deviceOffset( this ) ;
	connect( d,SIGNAL( offSetValue( QString,QString ) ),this,SLOT( deviceOffSet( QString,QString ) ) ) ;
	d->ShowUI_1() ;
}

void keyDialog::showFileSystemOptionWindow()
{
	mountOptions * m = new mountOptions( &m_options,this ) ;
	m->ShowUI() ;
}

void keyDialog::doAction( QAction * ac )
{
	QString e = ac->text() ;

	if( e == tr( "Set File System 0ptions" ) ){
		this->showFileSystemOptionWindow() ;
	}else if( e == tr( "Set Volume Offset" ) ){
		this->showOffSetWindowOption() ;
	}else if( e == tr( "Set Volume As VeraCrypt Volume" ) ){
		m_veraCryptVolume = true ;
	}
}

void keyDialog::deviceOffSet( QString deviceOffSet,QString key )
{
	m_deviceOffSet = QString( " -o %1" ).arg( deviceOffSet ) ;
	Q_UNUSED( key ) ;
}

void keyDialog::cbMountReadOnlyStateChanged( int state )
{
	m_ui->checkBoxOpenReadOnly->setEnabled( false ) ;
	m_ui->checkBoxOpenReadOnly->setChecked( utility::setOpenVolumeReadOnly( this,state == Qt::Checked,QString( "zuluMount-gui" ) ) ) ;
	m_ui->checkBoxOpenReadOnly->setEnabled( true ) ;
	if( m_ui->lineEditKey->text().isEmpty() ){
		m_ui->lineEditKey->setFocus() ;
	}else if( m_ui->lineEditMountPoint->text().isEmpty() ){
		m_ui->lineEditMountPoint->setFocus() ;
	}else{
		m_ui->pbOpen->setFocus() ;
	}
}

void keyDialog::pbMountPointPath()
{
	QString msg = tr( "Select A Folder To Create A Mount Point In" ) ;
	QString Z = QFileDialog::getExistingDirectory( this,msg,utility::homePath(),QFileDialog::ShowDirsOnly ) ;

	if( !Z.isEmpty() ){
		Z = Z + "/" + m_ui->lineEditMountPoint->text().split( "/" ).last() ;
		m_ui->lineEditMountPoint->setText( Z ) ;
	}
}

void keyDialog::enableAll()
{
	m_ui->pbOptions->setEnabled( true ) ;
	m_ui->label_2->setEnabled( true ) ;
	m_ui->lineEditMountPoint->setEnabled( true ) ;
	m_ui->pbOpenMountPoint->setEnabled( true ) ;
	m_ui->pbCancel->setEnabled( true ) ;
	m_ui->pbOpen->setEnabled( true ) ;
	m_ui->label->setEnabled( true ) ;
	m_ui->cbKeyType->setEnabled( true ) ;
	if( m_ui->cbKeyType->currentIndex() != keyDialog::plugin ){
		m_ui->lineEditKey->setEnabled( true ) ;
	}
	m_ui->pbkeyOption->setEnabled( true ) ;
	m_ui->checkBoxOpenReadOnly->setEnabled( true ) ;

	if( !m_volumeIsEncFs ){

		m_ui->checkBoxShareMountPoint->setEnabled( true ) ;
	}
}

void keyDialog::disableAll()
{
	m_ui->cbKeyType->setEnabled( false ) ;
	m_ui->pbOptions->setEnabled( false ) ;
	m_ui->pbkeyOption->setEnabled( false ) ;
	m_ui->label_2->setEnabled( false ) ;
	m_ui->lineEditMountPoint->setEnabled( false ) ;
	m_ui->pbOpenMountPoint->setEnabled( false ) ;
	m_ui->lineEditKey->setEnabled( false ) ;
	m_ui->pbCancel->setEnabled( false ) ;
	m_ui->pbOpen->setEnabled( false ) ;
	m_ui->label->setEnabled( false ) ;
	m_ui->checkBoxOpenReadOnly->setEnabled( false ) ;
	m_ui->checkBoxShareMountPoint->setEnabled( false ) ;
}

void keyDialog::KeyFile()
{
	if( m_ui->cbKeyType->currentIndex() == keyDialog::keyfile ){
		QString msg = tr( "Select A File To Be Used As A Keyfile" ) ;
		QString Z = QFileDialog::getOpenFileName( this,msg,utility::homePath() ) ;

		if( !Z.isEmpty() ){
			m_ui->lineEditKey->setText( Z ) ;
		}
	}
}

void keyDialog::pbkeyOption()
{
	if( m_ui->cbKeyType->currentIndex() == keyDialog::plugin ){
		this->Plugin() ;
	}else if( m_ui->cbKeyType->currentIndex() == keyDialog::keyfile ){
		this->KeyFile() ;
	}
}

void keyDialog::Plugin()
{
	utility::createPlugInMenu( m_menu,tr( INTERNAL_WALLET ),
				   tr( GNOME_WALLET ),tr( KWALLET ),!m_volumeIsEncFs ) ;

	m_menu->addSeparator() ;

	m_menu->addAction( tr( "Cancel" ) ) ;

	m_menu->exec( QCursor::pos() ) ;
}

void keyDialog::pbPluginEntryClicked( QAction * e )
{
	if( e->text() != tr( "Cancel" ) ){

		m_ui->lineEditKey->setText( e->text() ) ;
	}
}

void keyDialog::closeEvent( QCloseEvent * e )
{
	e->ignore() ;
	this->pbCancel() ;
}

void keyDialog::pbOpen()
{
	this->disableAll() ;

	if( m_ui->cbKeyType->currentIndex() == keyDialog::plugin ){

		utility::wallet w ;

		QString wallet = m_ui->lineEditKey->text() ;

		if( wallet == tr( KWALLET ) ){

			w = utility::getKeyFromWallet( LxQt::Wallet::kwalletBackEnd,m_path ) ;

		}else if( wallet == tr( INTERNAL_WALLET ) ){

			w = utility::getKeyFromWallet( LxQt::Wallet::internalBackEnd,m_path,_internalPassWord ) ;

			if( w.notConfigured ){

				DialogMsg msg( this ) ;
				msg.ShowUIOK( tr( "ERROR!" ),tr( "Internal wallet is not configured" ) ) ;
				return this->enableAll() ;
			}else{
				_internalPassWord = w.password ;
			}

		}else if( wallet == tr( GNOME_WALLET ) ){

			w = utility::getKeyFromWallet( LxQt::Wallet::secretServiceBackEnd,m_path ) ;
		}else{
			return this->openVolume() ;
		}

		if( w.opened ){

			if( w.key.isEmpty() ){
				DialogMsg msg( this ) ;
				msg.ShowUIOK( tr( "ERROR" ),tr( "The volume does not appear to have an entry in the wallet" ) ) ;
				this->enableAll() ;
				if( m_ui->cbKeyType->currentIndex() != keyDialog::Key ){
					m_ui->lineEditKey->setEnabled( false ) ;
				}
			}else{
				m_key = w.key ;
				this->openVolume() ;
			}
		}else{
			_internalPassWord.clear() ;
			this->enableAll() ;
		}
	}else{
		this->openVolume() ;
	}
}

void keyDialog::encfsMount()
{
	QString m = utility::mountPath( m_point ) ;

	bool ro = m_ui->checkBoxOpenReadOnly->isChecked() ;

	if( zuluMountTask::encfsMount( m_path,m,m_key,ro ).await() ){

		emit openMountPoint( m ) ;

		this->HideUI() ;
	}else{
		DialogMsg msg( this ) ;
		msg.ShowUIOK( tr( "ERROR" ),tr( "Failed to unlock an encfs volume.\nWrong password or not an encfs volume" ) ) ;
		if( m_ui->cbKeyType->currentIndex() == keyDialog::Key ){
			m_ui->lineEditKey->clear() ;
		}
		m_ui->lineEditKey->setFocus() ;
		return this->enableAll() ;
	}
}

void keyDialog::openVolume()
{
	int keyType = m_ui->cbKeyType->currentIndex() ;

	if( m_volumeIsEncFs ){

		if( keyType == keyDialog::Key ){

			m_key = m_ui->lineEditKey->text() ;

		}else if( keyType == keyDialog::keyfile ){

			QFile f( m_ui->lineEditKey->text() ) ;

			f.open( QIODevice::ReadOnly ) ;

			m_key = f.readAll() ;

		}else if( keyType == keyDialog::plugin ){

			/*
			 * m_key is already set
			 */
		}

		return this->encfsMount() ;
	}

	if( m_ui->lineEditKey->text().isEmpty() ){
		if( keyType == keyDialog::Key ){
			;
		}else if( keyType == keyDialog::plugin ){
			DialogMsg msg( this ) ;
			msg.ShowUIOK( tr( "ERROR" ),tr( "Plug in name field is empty" ) ) ;
			m_ui->lineEditKey->setFocus() ;
			return this->enableAll() ;
		}else if( keyType == keyDialog::keyfile ){
			DialogMsg msg( this ) ;
			msg.ShowUIOK( tr( "ERROR" ),tr( "Keyfile field is empty" ) ) ;
			m_ui->lineEditKey->setFocus() ;
			return this->enableAll() ;
		}
	}

	QString test_name = m_ui->lineEditMountPoint->text() ;
	if( test_name.contains( "/" ) ){
		DialogMsg msg( this ) ;
		msg.ShowUIOK( tr( "ERROR" ),tr( "\"/\" character is not allowed in the mount name field" ) ) ;
		m_ui->lineEditKey->setFocus() ;
		return this->enableAll() ;
	}

	QString m ;
	if( keyType == keyDialog::Key ){

		QString addr = utility::keyPath() ;
		m = QString( "-f %1" ).arg( addr ) ;

		QString key = m_ui->lineEditKey->text() ;

		utility::keySend( addr,key ) ;

	}else if( keyType == keyDialog::keyfile ){

		QString e = m_ui->lineEditKey->text().replace( "\"","\"\"\"" ) ;
		m = "-f \"" + utility::resolvePath( e ) + "\"" ;

	}else if( keyType == keyDialog::plugin ){

		if( m_key.isEmpty() ){

			m = "-G " + m_ui->lineEditKey->text().replace( "\"","\"\"\"" ) ;
		}else{

			QString addr = utility::keyPath() ;
			m = QString( "-f %1" ).arg( addr ) ;

			utility::keySend( addr,m_key ) ;
		}
	}else if( keyType == keyDialog::tcryptKeys ){

		QString addr = utility::keyPath() ;
		m = QString( "-f %1 " ).arg( addr ) ;

		utility::keySend( addr,m_key ) ;
	}else{
		qDebug() << "ERROR: Uncaught condition" ;
	}

	QString volume = m_path ;
	volume.replace( "\"","\"\"\"" ) ;

	QString exe = zuluMountPath ;

	if( m_ui->checkBoxShareMountPoint->isChecked() ){
		exe += " -M -m -d \"" + volume + "\"" ;
	}else{
		exe += " -m -d \"" + volume + "\"" ;
	}

	if( m_ui->checkBoxOpenReadOnly->isChecked() ){
		exe += " -e ro" ;
	}else{
		exe += "  e rw" ;
	}

	QString mountPoint = m_ui->lineEditMountPoint->text() ;
	mountPoint.replace( "\"","\"\"\"" ) ;

	exe += " -z \"" + mountPoint + "\"" ;

	if( !m_options.isEmpty() ){

		exe += " -Y " + m_options ;
	}

	if( !m_deviceOffSet.isEmpty() ){

		exe += " -o " + m_deviceOffSet ;
	}

	if( !m_keyFiles.isEmpty() ){

		for( const auto& it : m_keyFiles ){

			QString e = it ;
			e.replace( "\"","\"\"\"" ) ;

			exe += " -F \"" + e + "\"" ;
		}
	}

	if( m_veraCryptVolume ){

		exe += " -t vera " + m ;
	}else{
		exe += " " + m ;
	}

	m_veraCryptWarning.show( m_veraCryptVolume ) ;

	m_working = true ;

	auto s = utility::Task::run( utility::appendUserUID( exe ) ).await() ;

	m_working = false ;

	m_veraCryptWarning.stopTimer() ;

	if( s.success() ){

		if( utility::mapperPathExists( m_path ) ) {
			/*
			 * The volume is reported as opened and it actually is
			 */
			emit openMountPoint( utility::mountPath( m_point ) ) ;
		}else{
			/*
			 * The volume is reported as opened but it isnt,possible reason is a backe end crash
			 */

			DialogMsg msg( this ) ;

			msg.ShowUIOK( tr( "ERROR" ),tr( "An error has occured and the volume could not be opened" ) ) ;
			emit cancel() ;
		}
		this->HideUI() ;
	}else{
		m_veraCryptWarning.hide() ;

		if( s.exitCode() == 12 && m_ui->cbKeyType->currentIndex() == keyDialog::plugin ){
			/*
			 * A user cancelled the plugin
			 */
			this->enableAll() ;
		}else{

			QString z = s.output() ;
			z.replace( tr( "ERROR: " ),"" ) ;

			DialogMsg msg( this ) ;

			msg.ShowUIOK( tr( "ERROR" ),z ) ;

			if( m_ui->cbKeyType->currentIndex() == keyDialog::Key ){
				m_ui->lineEditKey->clear() ;
			}
			this->enableAll() ;
			m_ui->lineEditKey->setFocus() ;
		}
	}
}

void keyDialog::cbActicated( int e )
{
	switch( e ){
		case keyDialog::Key     : return this->key() ;
		case keyDialog::keyfile : return this->keyFile() ;
		case keyDialog::plugin  : return this->plugIn() ;
		case keyDialog::tcryptKeys : return this->tcryptGui() ;
	}
}

void keyDialog::plugIn()
{
	m_ui->pbkeyOption->setIcon( QIcon( ":/module.png" ) ) ;
	m_ui->lineEditKey->setEchoMode( QLineEdit::Normal ) ;
	m_ui->label->setText( tr( "Plugin name" ) ) ;
	m_ui->pbkeyOption->setEnabled( true ) ;
	m_ui->lineEditKey->setEnabled( false ) ;
	m_ui->lineEditKey->setText( INTERNAL_WALLET ) ;
}

void keyDialog::key()
{
	m_ui->pbkeyOption->setIcon( QIcon( ":/passphrase.png" ) ) ;
	m_ui->pbkeyOption->setEnabled( false ) ;
	m_ui->label->setText( tr( "Key" ) ) ;
	m_ui->lineEditKey->setEchoMode( QLineEdit::Password ) ;
	m_ui->lineEditKey->clear() ;
	m_ui->lineEditKey->setEnabled( true ) ;
}

void keyDialog::keyFile()
{
	m_ui->pbkeyOption->setIcon( QIcon( ":/keyfile.png" ) ) ;
	m_ui->lineEditKey->setEchoMode( QLineEdit::Normal ) ;
	m_ui->label->setText( tr( "Keyfile path" ) ) ;
	m_ui->pbkeyOption->setEnabled( true ) ;
	m_ui->lineEditKey->clear() ;
	m_ui->lineEditKey->setEnabled( true ) ;
}

void keyDialog::pbCancel()
{
	this->HideUI() ;
	emit cancel() ;
}

void keyDialog::ShowUI()
{
	this->show() ;
}

void keyDialog::HideUI()
{
	if( !m_working ){
		this->hide() ;
		this->deleteLater() ;
	}
}

keyDialog::~keyDialog()
{
	m_menu->deleteLater() ;
	delete m_ui ;
}
